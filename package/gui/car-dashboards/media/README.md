# Optional test media

The repository contains no audio or video files; they are not redistributable.
The HMI builds without them. To include your own copies of the tested files in
the image, run from the SDK root:

```bash
bash scripts/import-hmi-media.sh --audio /path/to/u_98o9hlkn7r-synthwave.mp3 \
    --video /path/to/big_buck_bunny_720p_h264.mov
```

The script checks the hashes below and never overwrites a different file.

| File | SHA-256 | Installed as |
| --- | --- | --- |
| `u_98o9hlkn7r-synthwave.mp3` | `afb92075295597e20ca407b35577c359442edd63753479768b8b642391578167` | `/usr/share/sounds/hz-ui/u_98o9hlkn7r-synthwave.mp3` |
| `big_buck_bunny_720p_h264.mov` | `45c8bafeb9a53df7f491198d2e71529701bcf1cd51805782089fac1d32869f9b` | `/usr/share/videos/hz-ui/big_buck_bunny_720p_h264.mov` |

The Music page plays the MP3, the Video page the MOV (1280×720 H.264).
