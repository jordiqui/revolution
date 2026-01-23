#!/bin/sh

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
evaluate_file="$script_dir/evaluate.h"

wget_or_curl=$(
  (command -v wget > /dev/null 2>&1 && echo "wget -qO-") || \
  (command -v curl > /dev/null 2>&1 && echo "curl -skL")
)

sha256sum=$(
  (command -v shasum > /dev/null 2>&1 && echo "shasum -a 256") || \
  (command -v sha256sum > /dev/null 2>&1 && echo "sha256sum")
)

if [ -z "$sha256sum" ]; then
  >&2 echo "Error: sha256sum or shasum -a 256 is required to validate NNUE files."
  exit 1
fi

if [ ! -f "$evaluate_file" ]; then
  >&2 echo "Error: expected $evaluate_file to exist."
  exit 1
fi

get_nnue_filename() {
  grep "$1" "$evaluate_file" | grep "#define" | sed "s/.*\(nn-[a-z0-9]\{12\}.nnue\).*/\1/"
}

validate_network() {
  if [ -f "$1" ]; then
    filename=$(basename "$1")
    hash="$($sha256sum "$1" | awk '{print $1}' | cut -c 1-12)"
    if [ "$filename" != "nn-$hash.nnue" ]; then
      rm -f "$1"
      return 1
    fi
    return 0
  fi
  return 1
}

fetch_network() {
  _filename="$(get_nnue_filename "$1")"

  if [ -z "$_filename" ]; then
    >&2 echo "NNUE file name not found for: $1"
    return 1
  fi

  target="$script_dir/$_filename"

  if [ -f "$target" ]; then
    if validate_network "$target"; then
      echo "Existing $_filename validated, skipping download"
      return 0
    else
      echo "Removing invalid NNUE file: $_filename"
    fi
  fi

  if [ -z "$wget_or_curl" ]; then
    >&2 printf "%s\n" "Neither wget or curl is installed." \
      "Install one of these tools to download NNUE files automatically."
    exit 1
  fi

  for url in \
    "https://tests.stockfishchess.org/api/nn/$_filename" \
    "https://github.com/official-stockfish/networks/raw/master/$_filename"; do
    echo "Downloading from $url ..."
    if $wget_or_curl "$url" > "$target"; then
      if validate_network "$target"; then
        echo "Successfully validated $_filename"
        return 0
      else
        echo "Downloaded $_filename is invalid"
        continue
      fi
    else
      echo "Failed to download from $url"
      rm -f "$target"
    fi
  done

  >&2 echo "Failed to download $_filename"
  return 1
}

fetch_network EvalFileDefaultNameBig && \
fetch_network EvalFileDefaultNameSmall
