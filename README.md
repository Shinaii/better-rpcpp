# Better-RPC++
Better-RPC++ is a tool for Discord RPC (Rich Presence) to let your friends know about your Linux system! based on [RPC++](https://github.com/grialion/rpcpp) by grialion.


## Installing requirements
### Arch based systems
```sh
pacman -S unzip
```
### Debian based systems
```sh
apt install unzip -y
```

## Building
**GNU Make**, and **Discord Game SDK** are **required**. To see more information about setting up Discord Game SDK, see [DISCORD.md](./DISCORD.md)

If you have Arch Linux, please read the AUR section.

To build Better-RPC++, use the command: 
```sh
make
```

## Installing & Running
To install RPC++, run the this command:
```sh
sudo make install
```
You can run the app from any directory with
```sh
brpc
```

To run manually (without installing) you need to start `./build/brpc` with the variables `LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$(pwd)/lib"`

## AUR
Will be coming in the future.

## Features
- Support for [Vesktop](https://github.com/Vencord/Vesktop)
- Displays your distro with an icon (supported: Arch, Gentoo, Mint, Ubuntu, Manjaro)
- Displays the focused window's class name with an icon (see supported apps [here](./APPLICATIONS.md))
- Displays CPU and RAM usage %
- Displays your window manager (WM)
- Displays your uptime
- Refreshes every second
  
![Preview of the rich presence](./screenshot.png)

## Will you add more application/distro support?
Feel free to open an issue and i'll add it.

## Contributing
Feel free to make a pull request!