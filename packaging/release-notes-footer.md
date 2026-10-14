---

### Installing this release

**Ubuntu 22.04 (amd64)** — download `papervision_<version>_amd64.deb` from the assets below.

Prerequisite: the Basler pylon 6.2 runtime (proprietary; not bundled). Install it once per machine
from the Basler download page:

```bash
sudo dpkg -i pylon_6.2.0.21487-deb0_amd64.deb
sudo apt-get -f install   # only if the line above reports missing dependencies
```

Then install or upgrade PaperVision with the attached package:

```bash
sudo apt install ./papervision_<version>_amd64.deb
```

- **Upgrade**: run the same command with the newer file; settings and recordings are kept.
- **Uninstall**: `sudo apt remove papervision` — recordings and settings stay on disk.
- **Ubuntu 20.04** keeps using the Docker image; both deployments share this source tree.

Details: [docs/installation.md](https://github.com/xlibraboy/web_vision_pro/blob/main/docs/installation.md).
