#!/usr/bin/env bash
# Shared helpers for CI scripts. Source this file, do not execute it.

# retry <attempts> <delay-seconds> <command...>: re-run a flaky network command with a growing delay.
retry() {
  local attempts="$1" delay="$2" n=1
  shift 2
  until "$@"; do
    if (( n >= attempts )); then
      echo "retry: giving up after ${n} attempts: $*" >&2
      return 1
    fi
    echo "retry: attempt ${n}/${attempts} failed, sleeping ${delay}s: $*" >&2
    sleep "$delay"
    n=$((n + 1))
    delay=$((delay * 2))
  done
}
