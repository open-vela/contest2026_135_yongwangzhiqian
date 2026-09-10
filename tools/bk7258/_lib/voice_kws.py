#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Offline candidate training for the BK7258 ``nihao_openvela`` wake-word model.

This module intentionally has no corpus discovery or upload behaviour.  The
operator supplies a consented, local manifest; audit reports are aggregate
only, and trained output remains a candidate until separately accepted.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import re
import stat
import subprocess
import tempfile
import wave
from collections import Counter
from pathlib import Path
from typing import Any, Iterable


SCHEMA = "bkvoice-kws-dataset-v1"
FRONTEND = "bkvoice-microfrontend-v1"
LABELS = ("silence", "unknown", "nihao_openvela")
WAKE_PHRASE = "你好，openvela"
SPLITS = ("train", "validation", "test")
SAMPLES = 32000
FEATURE_ROWS = 99
FEATURES = FEATURE_ROWS * 40


class KwsError(RuntimeError):
    """A non-sensitive manifest or training error."""


def add_arguments(subparsers: argparse._SubParsersAction[argparse.ArgumentParser]) -> None:
    """Add ``kws audit`` and ``kws train`` below the maintained voice CLI."""
    kws = subparsers.add_parser("kws", help="audit or train a local KWS candidate")
    commands = kws.add_subparsers(dest="kws_command", required=True)
    audit = commands.add_parser("audit", help="validate a consented local dataset manifest")
    audit.add_argument("--manifest", required=True, type=Path)
    train = commands.add_parser("train", help="train an INT8 candidate from a valid manifest")
    train.add_argument("--manifest", required=True, type=Path)
    train.add_argument("--output", required=True, type=Path,
                       help="new, non-existent candidate output directory")
    train.add_argument("--epochs", type=int, default=12)
    train.add_argument("--batch-size", type=int, default=16)
    train.add_argument("--seed", type=int, default=1337)


def _fail(reason: str) -> None:
    raise KwsError(reason)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _safe_audio(root: Path, value: Any) -> Path:
    if not isinstance(value, str) or not value:
        _fail("entry_path_invalid")
    relative = Path(value)
    if relative.is_absolute() or ".." in relative.parts:
        _fail("entry_path_invalid")
    candidate = root / relative
    # Refuse links in every supplied component, rather than merely resolving
    # the final target after it may already have escaped the dataset root.
    current = root
    for part in relative.parts:
        current = current / part
        try:
            mode = current.lstat().st_mode
        except OSError as error:
            raise KwsError("entry_unavailable") from error
        if stat.S_ISLNK(mode):
            _fail("entry_path_invalid")
    try:
        resolved = candidate.resolve(strict=True)
        resolved.relative_to(root)
        mode = candidate.lstat().st_mode
    except (OSError, ValueError) as error:
        raise KwsError("entry_unavailable") from error
    if not stat.S_ISREG(mode):
        _fail("entry_not_regular")
    return resolved


def _pcm16(path: Path) -> bytes:
    try:
        with wave.open(str(path), "rb") as audio:
            if (audio.getnchannels() != 1 or audio.getframerate() != 16000 or
                    audio.getsampwidth() != 2 or audio.getcomptype() != "NONE" or
                    audio.getnframes() != SAMPLES):
                _fail("audio_format_invalid")
            frames = audio.readframes(SAMPLES)
    except (OSError, EOFError, wave.Error) as error:
        raise KwsError("audio_format_invalid") from error
    if len(frames) != SAMPLES * 2:
        _fail("audio_format_invalid")
    return frames


def _read_manifest(path: Path) -> dict[str, Any]:
    try:
        if stat.S_ISLNK(path.lstat().st_mode) or not stat.S_ISREG(path.stat().st_mode):
            _fail("manifest_invalid")
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise KwsError("manifest_invalid") from error
    if not isinstance(data, dict):
        _fail("manifest_invalid")
    return data


def _validate(manifest_path: Path) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    document = _read_manifest(manifest_path)
    if (document.get("schema") != SCHEMA or document.get("frontend") != FRONTEND or
            document.get("labels") != list(LABELS) or not isinstance(document.get("entries"), list)):
        _fail("manifest_contract_invalid")
    root = manifest_path.parent.resolve()
    records: list[dict[str, Any]] = []
    speaker_splits: dict[str, set[str]] = {}
    hash_splits: dict[str, set[str]] = {}
    counts: Counter[tuple[str, str]] = Counter()
    for entry in document["entries"]:
        if not isinstance(entry, dict):
            _fail("entry_invalid")
        label, split, speaker, declared = (entry.get("label"), entry.get("split"),
                                            entry.get("speaker"), entry.get("sha256"))
        if label not in LABELS or split not in SPLITS:
            _fail("entry_label_or_split_invalid")
        if not isinstance(speaker, str) or not speaker or len(speaker) > 128:
            _fail("entry_speaker_invalid")
        if entry.get("consent") is not True:
            _fail("entry_consent_missing")
        if not isinstance(declared, str) or not re.fullmatch(r"[0-9a-f]{64}", declared):
            _fail("entry_hash_invalid")
        audio = _safe_audio(root, entry.get("path"))
        actual = _sha256(audio)
        if actual != declared:
            _fail("entry_hash_mismatch")
        pcm = _pcm16(audio)
        if label == "nihao_openvela" and not any(pcm):
            _fail("positive_audio_silent")
        speaker_splits.setdefault(speaker, set()).add(split)
        hash_splits.setdefault(hashlib.sha256(pcm).hexdigest(), set()).add(split)
        counts[(split, label)] += 1
        records.append({"path": audio, "split": split, "label": label,
                        "speaker": speaker, "sha256": actual, "pcm": pcm})
    if not records:
        _fail("dataset_empty")
    if any(len(value) > 1 for value in speaker_splits.values()):
        _fail("speaker_cross_split")
    if any(len(value) > 1 for value in hash_splits.values()):
        _fail("audio_cross_split")
    if any(counts[(split, label)] == 0 for split in SPLITS for label in LABELS):
        _fail("class_or_split_missing")
    identity = [{key: record[key] for key in ("split", "label", "speaker", "sha256")}
                for record in records]
    encoded = json.dumps(sorted(identity, key=lambda item: json.dumps(item, sort_keys=True)),
                         sort_keys=True, separators=(",", ":")).encode("utf-8")
    report = {"schema": SCHEMA, "frontend": FRONTEND, "labels": list(LABELS),
              "status": "candidate", "entries": len(records),
              "counts": {split: {label: counts[(split, label)] for label in LABELS}
                         for split in SPLITS},
              "dataset_sha256": hashlib.sha256(encoded).hexdigest()}
    return records, report


def audit(manifest: Path) -> dict[str, Any]:
    """Return a privacy-preserving aggregate audit report or raise ``KwsError``."""
    return _validate(manifest)[1]


def _frontend_inputs() -> tuple[Path, Path, Path, list[Path], list[Path], list[Path]]:
    """Resolve every local source/header consumed by the host frontend build."""
    source = Path(__file__).resolve().parents[3] / "app/bk7258/bk7258_voice_kws_frontend.c"
    workspace = source.parents[3]
    tflm = workspace / "apps/mlearning/tflite-micro/tflite-micro"
    frontend = tflm / "tensorflow/lite/experimental/microfrontend/lib"
    kissfft = workspace / "apps/math/kissfft/kissfft"
    sources = [
        source,
        frontend / "frontend.c", frontend / "frontend_util.c",
        frontend / "fft.cc", frontend / "fft_util.cc", frontend / "kiss_fft_int16.cc",
        frontend / "filterbank.c", frontend / "filterbank_util.c",
        frontend / "log_lut.c", frontend / "log_scale.c", frontend / "log_scale_util.c",
        frontend / "noise_reduction.c", frontend / "noise_reduction_util.c",
        frontend / "pcan_gain_control.c", frontend / "pcan_gain_control_util.c",
        frontend / "window.c", frontend / "window_util.c",
    ]
    dependencies = [kissfft / "kiss_fft.c", kissfft / "tools/kiss_fftr.c"]
    headers = [
        source.with_suffix(".h"),
        frontend / "bits.h", frontend / "frontend.h", frontend / "frontend_util.h",
        frontend / "fft.h", frontend / "fft_util.h", frontend / "kiss_fft_common.h",
        frontend / "kiss_fft_int16.h", frontend / "filterbank.h", frontend / "filterbank_util.h",
        frontend / "log_lut.h", frontend / "log_scale.h", frontend / "log_scale_util.h",
        frontend / "noise_reduction.h", frontend / "noise_reduction_util.h",
        frontend / "pcan_gain_control.h", frontend / "pcan_gain_control_util.h",
        frontend / "window.h", frontend / "window_util.h", kissfft / "kiss_fft.h",
        kissfft / "_kiss_fft_guts.h", kissfft / "tools/kiss_fftr.h",
    ]
    if any(not item.is_file() for item in (*sources, *dependencies, *headers)):
        _fail("frontend_source_unavailable")
    return workspace, tflm, kissfft, sources, dependencies, headers


def _frontend_provenance() -> dict[str, str]:
    """Return content hashes keyed by OpenVela-root-relative input paths."""
    workspace, _, _, sources, dependencies, headers = _frontend_inputs()
    try:
        return {item.relative_to(workspace).as_posix(): _sha256(item)
                for item in (*sources, *dependencies, *headers)}
    except OSError as error:
        raise KwsError("frontend_source_unavailable") from error


def _frontend_library() -> ctypes.CDLL:
    _, tflm, kissfft, sources, _, _ = _frontend_inputs()
    with tempfile.TemporaryDirectory(prefix="bkvoice-kws-frontend-") as temporary:
        output = Path(temporary) / "frontend.so"
        try:
            objects: list[Path] = []
            for index, item in enumerate(sources):
                object_file = Path(temporary) / f"frontend-{index}.o"
                compiler = "c++" if item.suffix == ".cc" else "cc"
                standard = "-std=c++17" if item.suffix == ".cc" else "-std=c11"
                result = subprocess.run([compiler, "-c", "-fPIC", standard, "-O2",
                                         f"-I{tflm}", f"-I{kissfft}", str(item),
                                         "-o", str(object_file)], check=False,
                                        capture_output=True, timeout=30)
                if result.returncode != 0:
                    _fail("frontend_compile_failed")
                objects.append(object_file)
            result = subprocess.run(["c++", "-shared", "-o", str(output),
                                     *(str(item) for item in objects), "-lm"],
                                    check=False, capture_output=True, timeout=30)
        except (OSError, subprocess.TimeoutExpired) as error:
            raise KwsError("frontend_compile_failed") from error
        if result.returncode != 0:
            _fail("frontend_compile_failed")
        # CDLL keeps the mapped object usable after TemporaryDirectory exits.
        library = ctypes.CDLL(str(output))
    function = library.bkvoice_kws_features
    function.argtypes = [ctypes.POINTER(ctypes.c_int16), ctypes.c_size_t,
                         ctypes.POINTER(ctypes.c_float), ctypes.c_size_t]
    function.restype = ctypes.c_int
    return library


def _features(records: Iterable[dict[str, Any]], numpy: Any) -> Any:
    library = _frontend_library()
    function = library.bkvoice_kws_features
    output = []
    for record in records:
        pcm = (ctypes.c_int16 * SAMPLES).from_buffer_copy(record["pcm"])
        feature = (ctypes.c_float * FEATURES)()
        if function(pcm, SAMPLES, feature, FEATURES) != 0:
            _fail("frontend_feature_failed")
        output.append(numpy.ctypeslib.as_array(feature).copy().reshape(FEATURE_ROWS, 40, 1))
    return numpy.stack(output).astype(numpy.float32)


def _rate(numerator: int, denominator: int) -> float | None:
    return None if denominator == 0 else numerator / denominator


def _evaluate_int8(interpreter: Any, features: Any, labels: Any, numpy: Any) -> dict[str, Any]:
    details_in = interpreter.get_input_details()[0]
    details_out = interpreter.get_output_details()[0]
    scale, zero = details_in["quantization"]
    if not scale:
        _fail("tflite_input_not_quantized")
    predicted = []
    for item in features:
        scaled = item / scale
        rounded = numpy.copysign(numpy.floor(numpy.abs(scaled) + 0.5), scaled)
        value = numpy.clip(rounded + zero, -128, 127).astype(numpy.int8)[None, ...]
        interpreter.set_tensor(details_in["index"], value)
        interpreter.invoke()
        predicted.append(int(numpy.argmax(interpreter.get_tensor(details_out["index"])[0])))
    labels = numpy.asarray(labels)
    predicted = numpy.asarray(predicted)
    positives = labels == 2
    unknown = labels == 1
    return {"positive_false_negative_rate": _rate(int(numpy.sum(positives & (predicted != 2))),
                                                    int(numpy.sum(positives))),
            "unknown_false_positive_rate": _rate(int(numpy.sum(unknown & (predicted == 2))),
                                                   int(numpy.sum(unknown))),
            "positive_samples": int(numpy.sum(positives)),
            "unknown_samples": int(numpy.sum(unknown))}


def train(manifest: Path, output: Path, *, epochs: int, batch_size: int, seed: int) -> dict[str, Any]:
    """Train and export a full-INT8 candidate; imports ML packages only here."""
    if epochs < 1 or batch_size < 1 or output.exists() or not output.parent.is_dir():
        _fail("training_arguments_invalid")
    records, report = _validate(manifest)
    try:
        import numpy as np
        import tensorflow as tf
    except ImportError as error:
        raise KwsError("training_dependencies_unavailable") from error
    tf.keras.utils.set_random_seed(seed)
    try:
        tf.config.experimental.enable_op_determinism()
    except (AttributeError, RuntimeError):
        pass
    features = _features(records, np)
    targets = np.asarray([LABELS.index(record["label"]) for record in records], dtype=np.int32)
    split = np.asarray([record["split"] for record in records])
    train_mask = split == "train"
    validation_mask = split == "validation"
    model = tf.keras.Sequential([tf.keras.layers.Input((FEATURE_ROWS, 40, 1)),
        tf.keras.layers.Conv2D(32, (10, 4), strides=(2, 2), padding="same", activation="relu"),
        *[layer for _ in range(4) for layer in (tf.keras.layers.DepthwiseConv2D((3, 3), padding="same", activation="relu"),
                                                tf.keras.layers.Conv2D(32, (1, 1), activation="relu"))],
        tf.keras.layers.AveragePooling2D(((FEATURE_ROWS + 1) // 2, 20)),
        tf.keras.layers.Flatten(),
        tf.keras.layers.Dense(3, activation="softmax")])
    model.compile(optimizer="adam", loss="sparse_categorical_crossentropy", metrics=["accuracy"])
    model.fit(features[train_mask], targets[train_mask], epochs=epochs, batch_size=batch_size,
              validation_data=(features[validation_mask], targets[validation_mask]), verbose=0)
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = lambda: ([features[index:index + 1]]
                                                  for index in np.where(train_mask)[0])
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    candidate = converter.convert()
    output.mkdir(mode=0o700)
    model_path = output / "model_int8.tflite"
    model_path.write_bytes(candidate)
    interpreter = tf.lite.Interpreter(model_path=str(model_path))
    interpreter.allocate_tensors()
    metrics = {name: _evaluate_int8(interpreter, features[split == name], targets[split == name], np)
               for name in ("validation", "test")}
    in_q = interpreter.get_input_details()[0]["quantization"]
    out_q = interpreter.get_output_details()[0]["quantization"]
    metadata = {**report, "wake_phrase": WAKE_PHRASE, "audio_seconds": 2,
                "architecture": "ds-cnn-conv32-10x4-s2-4x(dw3x3,pw32)-gap-dense3",
                "seed": seed, "tensorflow_version": tf.__version__, "model_sha256": _sha256(model_path),
                "epochs": epochs, "batch_size": batch_size,
                "frontend_inputs_sha256": _frontend_provenance(),
                "training_source_sha256": _sha256(Path(__file__)),
                "input_shape": [1, FEATURE_ROWS, 40, 1],
                "output_shape": [1, len(LABELS)],
                "input_quantization": [float(in_q[0]), int(in_q[1])],
                "output_quantization": [float(out_q[0]), int(out_q[1])], "metrics": metrics}
    (output / "metadata.json").write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n",
                                            encoding="utf-8")
    return metadata


def run(args: argparse.Namespace) -> dict[str, Any]:
    """CLI dispatcher used by the parent ``voice`` command."""
    if args.kws_command == "audit":
        return audit(args.manifest)
    if args.kws_command == "train":
        return train(args.manifest, args.output, epochs=args.epochs,
                     batch_size=args.batch_size, seed=args.seed)
    raise KwsError("command_invalid")
