#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$repo_root"

targets=(
  "assignment_1"
  "rts_demo"
  "rts_mass_battle_demo"
  "pathfinding_lab_demo"
  "building_siege_demo"
  "ai_vs_ai_battle_demo"
  "economy_race_demo"
  "rts_stress_test_demo"
  "tool_pipeline_demo"
  "doom_demo"
)

menu_items=(
  "assignment_1 - Assignment showcase"
  "rts_demo - RTS main demo"
  "rts_mass_battle_demo - RTS mass battle demo"
  "pathfinding_lab_demo - RTS pathfinding lab demo"
  "building_siege_demo - RTS building siege demo"
  "ai_vs_ai_battle_demo - RTS AI vs AI battle demo"
  "economy_race_demo - RTS economy race demo"
  "rts_stress_test_demo - RTS stress test demo"
  "tool_pipeline_demo - Tool pipeline demo"
  "doom_demo - Doom-style demo"
  "Quit"
)

labels=(
  "Assignment showcase"
  "RTS main demo"
  "RTS mass battle demo"
  "RTS pathfinding lab demo"
  "RTS building siege demo"
  "RTS AI vs AI battle demo"
  "RTS economy race demo"
  "RTS stress test demo"
  "Tool pipeline demo"
  "Doom-style demo"
)

descriptions=(
  "Assignment 3/4 showcase with managed objects, animation, collision, and deferred lighting."
  "Main RTS demo with selection, commands, economy, buildings, production, combat, fog, and AI."
  "Large two-army battle over multiple routes."
  "Pathfinding-focused RTS scenario with maze-like terrain."
  "Fortified base siege with towers, repairs, and waves."
  "Autonomous AI teams harvest, produce, scout, and attack."
  "Mirrored AI bases harvest, drop off, queue production, and scale without combat pressure."
  "High-unit-count RTS stress scenario."
  "Meshbin/animbin discovery, exported asset loading, animation sidecars, and deferred render submission."
  "Small Doom-style box fight demo."
)

candidate_build_dirs=()
if [[ -n "${BUILD_DIR:-}" ]]; then
  candidate_build_dirs+=("$BUILD_DIR")
fi
candidate_build_dirs+=("build_a3" "build" "cmake-build-debug" "cmake-build-release")

usage() {
  cat <<'USAGE'
Usage:
  ./run_demo.sh
  ./run_demo.sh <target-or-number>

Optional:
  BUILD_DIR=build_a3 ./run_demo.sh

Examples:
  ./run_demo.sh rts_demo
  ./run_demo.sh 6
  BUILD_DIR=build ./run_demo.sh pathfinding_lab_demo
USAGE
}

is_number() {
  [[ "$1" =~ ^[0-9]+$ ]]
}

resolve_target() {
  local choice="$1"

  if is_number "$choice"; then
    local index=$((choice - 1))
    if (( index >= 0 && index < ${#targets[@]} )); then
      printf "%s\n" "${targets[$index]}"
      return 0
    fi
    return 1
  fi

  for target in "${targets[@]}"; do
    if [[ "$choice" == "$target" ]]; then
      printf "%s\n" "$target"
      return 0
    fi
  done

  return 1
}

find_build_dir() {
  for dir in "${candidate_build_dirs[@]}"; do
    if [[ -f "$dir/CMakeCache.txt" ]] && cmake_cache_matches_repo "$dir"; then
      printf "%s\n" "$dir"
      return 0
    fi
  done
  return 1
}

find_executable() {
  local target="$1"
  for dir in "${candidate_build_dirs[@]}"; do
    if [[ -f "$dir/CMakeCache.txt" ]] && ! cmake_cache_matches_repo "$dir"; then
      continue
    fi
    if [[ -x "$dir/$target" ]]; then
      printf "%s\n" "$dir/$target"
      return 0
    fi
  done
  return 1
}

cmake_cache_matches_repo() {
  local dir="$1"
  local cache_home

  cache_home="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$dir/CMakeCache.txt" 2>/dev/null || true)"
  [[ -z "$cache_home" || "$cache_home" == "$repo_root" ]]
}

build_target() {
  local target="$1"
  local build_dir

  if ! build_dir="$(find_build_dir)"; then
    echo "No configured CMake build directory found."
    echo "Configure one first, for example:"
    echo "  cmake -S . -B build"
    return 1
  fi

  echo "Building $target in $build_dir..."
  cmake --build "$build_dir" --target "$target"
}

run_target() {
  local target="$1"
  local executable

  if ! executable="$(find_executable "$target")"; then
    echo
    echo "Executable for '$target' was not found in the known build directories."
    read -r -p "Build it now? [Y/n] " answer
    answer="${answer:-Y}"
    if [[ "$answer" =~ ^[Yy]$ ]]; then
      build_target "$target"
      executable="$(find_executable "$target")"
    else
      echo "Skipped."
      return 1
    fi
  fi

  echo
  echo "Running $target..."
  echo "$executable"
  echo
  "$executable"
}

select_demo() {
  local item target index

  echo
  echo "Demo selector"
  echo "-------------"
  echo "Pick a demo by number. Press Ctrl+C to exit."
  echo

  PS3=$'\nSelect demo: '
  select item in "${menu_items[@]}"; do
    if [[ -z "${item:-}" ]]; then
      echo "Invalid selection: $REPLY"
      continue
    fi

    if [[ "$item" == "Quit" ]]; then
      exit 0
    fi

    index=$((REPLY - 1))
    target="${targets[$index]}"

    echo
    echo "${labels[$index]}"
    echo "${descriptions[$index]}"
    run_target "$target"
    exit $?
  done
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ $# -gt 0 ]]; then
  if ! target="$(resolve_target "$1")"; then
    echo "Unknown demo selection: $1"
    usage
    exit 1
  fi
  run_target "$target"
  exit $?
fi

select_demo
