"""Exercise file resolution, take preservation and block rendering through the CLI."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import wave

cli = Path(sys.argv[1]).resolve()


def run(cwd, *args, success=True):
    result = subprocess.run([cli, *args], cwd=cwd, capture_output=True, text=True)
    assert (result.returncode == 0) == success, (result.stdout, result.stderr)
    return result


def source(path, value):
    with wave.open(str(path), "wb") as wav:
        wav.setparams((1, 2, 8000, 0, "NONE", ""))
        wav.writeframes(struct.pack("<h", value) * 8192)


with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    library = root / "song" / "lib"
    library.mkdir(parents=True)
    wav = library / "tone.wav"
    source(wav, 8192)
    source(root / "tone.wav", 16384)
    (library / "sample.sub").write_text(
        's = sampler { file: "tone.wav", loop: on }\nmain = output {}\ns -> main\n'
    )
    patch = root / "song" / "score.patch"
    patch.write_text('voice1 = sub { file: "lib/sample.sub" }\nvoice1.main -> out.in\n')
    mix = root / "mix.wav"
    args = ["render", patch, "--rate", "8000", "-d", "1.25s", "-o", mix]
    result = run(root, *args, "--tap", "part=voice1.main", "--stats", "0.6s")
    assert result.stdout.count("1.2s  rms") == 2, result.stdout
    stem = root / "mix.part.wav"
    for path, channels in [(mix, 2), (stem, 1)]:
        audio = path.read_bytes()
        assert len(audio) == 44 + 10000 * channels * 4
        assert audio[44:] == struct.pack("<f", 0.25) * (10000 * channels)

    wav.unlink()
    run(root, *args)
    assert struct.unpack_from("<f", mix.read_bytes(), 44)[0] == 0.5
    (root / "tone.wav").unlink()
    assert str(wav) in run(root, "check", patch).stderr

    def recorder(destination, value):
        patch.write_text(
            f'r = rec {{ file: "{destination}", secs: 1 }}\n'
            f'{value} -> r.in_l\n{value} -> out.in\n'
        )

    recorder("take.wav", 0.25)
    run(root, *args)
    original = (root / "take.wav").read_bytes()
    recorder("take.wav", 0.75)
    run(root, *args)
    assert (root / "take.wav").read_bytes() == original
    assert struct.unpack_from("<f", (root / "take.2.wav").read_bytes(), 44)[0] == 0.75
    recorder("missing/take.wav", 1)
    assert "missing/take.wav" in run(root, *args, success=False).stderr
