# A9K11AIO
All-in-one `.3dsx` executable for gaining ARM9 code execution

This is a combination of [udsploit](https://github.com/smealum/udsploit) and [safehax](https://github.com/TiniVi/safehax). Just run the `.3dsx` and it will automatically patch kernel, and then run safehax to execute a custom ARM9 binary.  
This program will try to load `safehaxpayload.bin` that's on SD root, but if it fails, it will fallback to a bundled ARM9 binary that has been extracted to a hex array and embedded in the source. Note that this is optional, and is not required for compiling.  
Note that you don't *have* to use this as a `boot.3dsx`, it can be used as a normal `.3dsx` from [hbmenu](https://github.com/fincs/new-hbmenu)

I don't want to take much credit because most of the work here was done by [Smealum](https://github.com/smealum/) and [TiniVi](https://github.com/TiniVi/). All I did was take the source code from those 2 repositories, clean it up/improve readability, add a couple of features, and release it.
