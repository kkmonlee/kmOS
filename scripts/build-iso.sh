#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
make -C "$repo_dir/src/kernel"
make -C "$repo_dir/src/userland"
iso_stage="$(mktemp -d)"
trap 'rm -rf "$iso_stage"' EXIT
mkdir -p "$iso_stage/tree/boot/grub"
cp "$repo_dir/src/kernel/kernel.elf" "$iso_stage/tree/boot/kernel.elf"
for program in init hello cat; do
    cp "$repo_dir/src/userland/$program.elf" "$iso_stage/tree/boot/$program.elf"
done
cp "$repo_dir/iso/boot/grub/grub.cfg" "$iso_stage/tree/boot/grub/grub.cfg"

if command -v grub-mkrescue >/dev/null; then
    grub-mkrescue -o "$iso_stage/kmos.iso" "$iso_stage/tree"
elif command -v grub2-mkrescue >/dev/null; then
    grub2-mkrescue -o "$iso_stage/kmos.iso" "$iso_stage/tree"
else
    if ! command -v docker >/dev/null || ! docker info >/dev/null 2>&1; then
        echo 'Install grub-mkrescue and xorriso, or start Docker/OrbStack.' >&2
        exit 1
    fi
    if ! docker image inspect kmos-iso-builder >/dev/null 2>&1; then
        docker build -t kmos-iso-builder -f "$repo_dir/Dockerfile.iso" "$repo_dir"
    fi
    docker run --rm --entrypoint grub-mkrescue \
        -v "$iso_stage:/build" kmos-iso-builder \
        -o /build/kmos.iso /build/tree
fi
cp "$iso_stage/kmos.iso" "$repo_dir/kmos.iso"
echo 'Built kmos.iso'
