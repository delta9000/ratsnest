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

namespace {
// Pops at most `cap` items: a producer pushing concurrently cannot keep the
// loop alive. Leftovers stay queued, in order, for the next block.
template <typename Q, typename T, typename F>
void drain_bounded(Q& q, T& item, size_t cap, F&& deliver) {
  for (size_t n = 0; n < cap && q.try_pop(item); ++n) deliver(item);
}
}  // namespace

void RtRunner::drain_commands() {
  // A command is only taken while the retire queue has room, so a Swap can
  // always hand off the fading engine; otherwise the command, and everything
  // behind it, waits (in order) for the non-RT side to reap.
  Command c;
  for (size_t n = 0;
       n < kCommandRing && retired_.write_available() > 0 && commands_.try_pop(c); ++n) {
    switch (c.kind) {
      case Command::Kind::SetParam:
        if (current_) current_->set_param(c.node, c.slot, c.value, glide_samples_);
        break;
      case Command::Kind::Swap:
        ++generation_;
        if (old_) retired_.try_push(old_);  // a save landed mid-fade; room checked
        old_ = nullptr;
        if (!current_) {
          current_ = c.engine;  // first graph: adopt immediately
        } else {
          c.engine->migrate_from(*current_);
          old_ = current_;
          current_ = c.engine;
          fade_pos_ = 0;
        }
        break;
      case Command::Kind::Observe:
        observing_ = c.value > 0;
        break;
      case Command::Kind::Watch:
        watch_node_ = c.node;
        watch_slot_ = c.slot;
        break;
    }
  }
}

// Block-start: drain the rings owned by *this* runner (not current_ -- see
// rt_runner.h) into the live engine, quantizing queued MIDI and captured
// audio to this block boundary. Each drain is capped; leftovers wait a block.
void RtRunner::drain_inputs() {
  dsp::MidiEvent midi_e;
  drain_bounded(midi_ring_, midi_e, kMidiRing,
                [this](const dsp::MidiEvent& e) { current_->deliver_midi_event(e); });
  AudioChunk audio_c;
  drain_bounded(audio_ring_, audio_c, kAudioRing,
                [this](const AudioChunk& a) { current_->deliver_audio(a.s, a.len); });
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

  drain_inputs();  // current_ is non-null here (checked above)
  current_->process(l, r, frames);
  frame_ += frames;
  since_observation_ += frames;
  if (observing_ && since_observation_ >= static_cast<size_t>(rate_ / 30)) {
    observation_.generation = generation_;
    observation_.frame = frame_;
    observation_.size = current_->copy_outputs(observation_.values);
    observations_.try_push(observation_);
    since_observation_ = 0;
  }

  if (old_) {
    old_->process(scratch_l_.data(), scratch_r_.data(), frames);
    for (size_t i = 0; i < frames; ++i) {
      const size_t p = fade_pos_ + i;
      const float t = p >= fade_samples_
          ? 1.0f
          : static_cast<float>(p) / static_cast<float>(fade_samples_);
      // Migrated graphs are often identical or strongly correlated. A linear
      // fade preserves their gain and the float headroom of each graph.
      l[i] = l[i] * t + scratch_l_[i] * (1.0f - t);
      r[i] = r[i] * t + scratch_r_[i] * (1.0f - t);
    }
    fade_pos_ += frames;
    // Fade done: retire. With the queue full, old_ lingers at zero gain and
    // is retried next block rather than dropped.
    if (fade_pos_ >= fade_samples_ && retired_.try_push(old_)) old_ = nullptr;
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
