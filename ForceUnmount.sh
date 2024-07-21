#!/bin/bash

# Get a list of all mounted FUSE filesystems
fuse_mounts=$(mount | grep 'fuse' | awk '{print $3}')

# Unmount each FUSE filesystem
for mount_point in $fuse_mounts; {
    echo "Forcefully unmounting $mount_point"
    sudo umount -f "$mount_point"
}

echo "All FUSE filesystems have been forcefully unmounted."