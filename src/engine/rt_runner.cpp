#include "engine/rt_runner.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace ms::engine {

RtRunner::RtRunner(double sample_rate)
    : rate_(sample_rate),
      glide_samples_(static_cast<size_t>(sample_rate * 0.020)),
      fade_samples_(static_cast<size_t>(sample_rate * 0.050)) {}

RtRunner::~RtRunner() {
  // Only safe once the audio thread has stopped calling render().
  Command c;
  while (commands_.try_pop(c))
    if (c.kind == Command::Kind::Swap) delete c.engine;
  Engine* e = nullptr;
  while (retired_.try_pop(e)) delete e;
  delete current_;
  delete old_;
}

void RtRunner::drain_commands() {
  Command c;
  while (commands_.try_pop(c)) {
    switch (c.kind) {
      case Command::Kind::SetParam:
        if (current_) current_->set_param(c.node, c.slot, c.value, glide_samples_);
        break;
      case Command::Kind::Swap:
        if (old_) {  // a second save landed mid-fade: retire the fading engine
          retired_.try_push(old_);  // capacity is gated by the controller
          old_ = nullptr;
        }
        if (!current_) {
          current_ = c.engine;  // first graph: adopt immediately
        } else {
          c.engine->migrate_from(*current_);
          old_ = current_;
          current_ = c.engine;
          fade_pos_ = 0;
        }
        break;
      case Command::Kind::Watch:
        watch_node_ = c.node;
        watch_slot_ = c.slot;
        break;
    }
  }
}

void RtRunner::render(float* l, float* r, size_t frames) {
  while (frames > kMaxBlock) {
    render_block(l, r, kMaxBlock);
    l += kMaxBlock;
    r += kMaxBlock;
    frames -= kMaxBlock;
  }
  render_block(l, r, frames);
}

void RtRunner::render_block(float* l, float* r, size_t frames) {
  drain_commands();
  if (!current_) {
    std::fill(l, l + frames, 0.0f);
    std::fill(r, r + frames, 0.0f);
    peak_l_.store(0.0f, std::memory_order_release);
    peak_r_.store(0.0f, std::memory_order_release);
    return;
  }

  // Block-start: drain the ring owned by *this* runner (not current_ --
  // see rt_runner.h) into the live engine, quantizing queued MIDI to this
  // block boundary. current_ is guaranteed non-null here (checked above).
  dsp::MidiEvent midi_e;
  while (midi_ring_.try_pop(midi_e)) current_->deliver_midi_event(midi_e);
  // Same for captured external audio: hand each queued chunk to the live
  // engine's extin nodes before processing this block.
  AudioChunk audio_c;
  while (audio_ring_.try_pop(audio_c)) current_->deliver_audio(audio_c.s, audio_c.len);
  current_->process(l, r, frames);

  if (old_) {
    old_->process(scratch_l_.data(), scratch_r_.data(), frames);
    constexpr float kHalfPi = std::numbers::pi_v<float> / 2.0f;
    for (size_t i = 0; i < frames; ++i) {
      const size_t p = fade_pos_ + i;
      const float t = p >= fade_samples_
          ? 1.0f
          : static_cast<float>(p) / static_cast<float>(fade_samples_);
      const float gn = std::sin(t * kHalfPi);
      const float go = std::cos(t * kHalfPi);
      l[i] = l[i] * gn + scratch_l_[i] * go;
      r[i] = r[i] * gn + scratch_r_[i] * go;
    }
    fade_pos_ += frames;
    if (fade_pos_ >= fade_samples_) {
      retired_.try_push(old_);
      old_ = nullptr;
    }
  }

  float pk_l = 0.0f, pk_r = 0.0f;
  for (size_t i = 0; i < frames; ++i) {
    pk_l = std::max(pk_l, std::abs(l[i]));
    pk_r = std::max(pk_r, std::abs(r[i]));
  }
  const float release = 3.0f * static_cast<float>(frames) / static_cast<float>(rate_);
  env_l_ = std::max(pk_l, env_l_ - release);
  env_r_ = std::max(pk_r, env_r_ - release);
  peak_l_.store(env_l_, std::memory_order_release);
  peak_r_.store(env_r_, std::memory_order_release);

  if (watch_node_ != kWatchOff)
    watch_.try_push(current_->output_value(watch_node_, watch_slot_));
}

}  // namespace ms::engine
