# Plugin blocking loading of additional tpm files in Kirikiri

This plugin blocks loading of additional .tpm files at startup in Kirikiri2 / 吉里吉里2 / KirikiriZ / 吉里吉里Z  

## Building

After cloning submodules, a simple `make` will generate `krblocktpm.dll`.

## How to use

Rename the plugin to `xxx.tpm`, where `xxx` is a name in ASCII alphabetical order such that the plugins wanted to load is before and the plugins not wanted to load is afterwards.  

### Example

```
00patch.tpm <-- This file will be loaded
01krblocktpm.tpm <-- This file contains the contents of krblocktpm.dll
encryption.tpm <-- This file will NOT be loaded
system/encryption.tpm <-- This file will NOT be loaded
plugin/encryption.tpm <-- This file will NOT be loaded
gamename.tpm <-- This file will NOT be loaded
system/gamename.tpm <-- This file will NOT be loaded
plugin/gamename.tpm <-- This file will NOT be loaded
ゲーム名.tpm <-- This file will NOT be loaded
system/ゲーム名.tpm <-- This file will NOT be loaded
plugin/ゲーム名.tpm <-- This file will NOT be loaded
```

## License

This project is licensed under the MIT license. Please read the `LICENSE` file for more information.
