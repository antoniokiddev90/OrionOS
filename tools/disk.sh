#!/usr/bin/env bash
set -euo pipefail
root_dir="$(cd "$(dirname "$0")/.." && pwd)"
image="${1:-$root_dir/build/disk.img}"
user_dir="${2:-$root_dir/build/user}"
size_mb="${3:-32}"
if [[ "$user_dir" != /* ]]; then user_dir="$root_dir/$user_dir"; fi
for tool in dd mke2fs debugfs; do
    command -v "$tool" >/dev/null 2>&1 || { printf 'required tool not found: %s\n' "$tool" >&2; exit 1; }
done
if [[ ! -f "$user_dir/init.elf" ]]; then
    printf 'init ELF not found: %s\n' "$user_dir/init.elf" >&2
    exit 1
fi
mkdir -p "$(dirname "$image")"
mkdir -p "$root_dir/build"
image_commands="$root_dir/build/ext2.commands"
motd_file="$root_dir/build/motd"
printf 'Welcome to OrionOS.\nType help to list available commands.\n' > "$motd_file"
passwd_file="$root_dir/build/passwd"
shadow_file="$root_dir/build/shadow"
pkg_repo_file="$root_dir/build/repo.list"
printf 'https://orionos-repo.onrender.com\n' > "$pkg_repo_file"
printf 'root:x:0:0:root:/root:/bin/sh\nantonio:x:1000:1000:antonio:/home/antonio:/bin/sh\n' > "$passwd_file"
hash_entry() {
    local salt hash
    salt="$(od -An -N8 -tx1 /dev/urandom | tr -d ' \n')"
    hash="$(printf '%s%s' "$salt" "$1" | sha256sum | cut -d' ' -f1)"
    printf '$sha256$%s$%s' "$salt" "$hash"
}
printf 'root:%s\nantonio:%s\n' "$(hash_entry root)" "$(hash_entry antonio)" > "$shadow_file"
sudoers_file="$root_dir/build/sudoers"
printf 'antonio\n' > "$sudoers_file"
dd if=/dev/zero of="$image" bs=1M count="$size_mb" status=none
mke2fs -q -F -t ext2 -b 1024 -m 0 "$image"
printf 'mkdir /bin\nmkdir /etc\nmkdir /dev\nmkdir /proc\nmkdir /tmp\nmkdir /root\nmkdir /home\nmkdir /home/antonio\nmkdir /etc/pkg\nmkdir /etc/pkg/db\nwrite %s /etc/motd\nwrite %s /etc/passwd\nwrite %s /etc/shadow\nwrite %s /etc/sudoers\nwrite %s /etc/pkg/repo.list\nwrite %s /init\nset_inode_field /init mode 0100755\nset_inode_field /etc/motd mode 0100644\nset_inode_field /etc/passwd mode 0100644\nset_inode_field /etc/shadow mode 0100600\nset_inode_field /etc/sudoers mode 0100440\nset_inode_field /etc/pkg/repo.list mode 0100644\nset_inode_field /etc/pkg mode 040755\nset_inode_field /etc/pkg/db mode 040755\nset_inode_field /root mode 040700\nset_inode_field /home/antonio uid 1000\nset_inode_field /home/antonio gid 1000\nset_inode_field /home/antonio mode 040755\nset_inode_field /tmp mode 041777\n' "$motd_file" "$passwd_file" "$shadow_file" "$sudoers_file" "$pkg_repo_file" "$user_dir/init.elf" > "$image_commands"
for executable in "$user_dir"/*.elf; do
    [[ -f "$executable" ]] || continue
    name="$(basename "$executable" .elf)"
    [[ "$name" == init ]] && continue
    printf 'write %s /bin/%s\nset_inode_field /bin/%s mode 0100755\n' "$executable" "$name" "$name" >> "$image_commands"
done
printf 'set_inode_field /bin/sudo mode 0104755\nset_inode_field /bin/passwd mode 0104755\n' >> "$image_commands"
debugfs -w -f "$image_commands" "$image" >/dev/null 2>&1
printf 'created ext2 rootfs: %s\n' "$image"
printf 'installed programs: %s\n' "$(find "$user_dir" -maxdepth 1 -name '*.elf' | wc -l)"
