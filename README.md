# OpenRTM Launcher

OpenRTM Launcher is the easiest way to start and keep OpenRTM up to date on
Windows and Linux. The launcher is a standalone native application, so it opens
even when Java has not been installed yet.

## Windows

1. Download `OpenRTM_Launcher.exe`.
2. Open the downloaded file.
3. Select **Install & Launch** or **Launch OpenRTM**.

No launcher installation or extra DLLs are required.

## Linux

Download the `openrtm` file, then make it executable and open it:

```sh
chmod +x openrtm
./openrtm
```

Select **Install** in the launcher to add OpenRTM to the application menu in
GNOME, KDE Plasma, and other common desktop environments. This installs it only
for the current user and does not require administrator access.

## Java 17

OpenRTM requires a JDK version 17 or newer. The launcher checks `JAVA_HOME`, the
system path, and common Java installation folders automatically. When no
compatible JDK is found, select **Get OpenJDK 17** to open the correct Eclipse
Temurin download page for your operating system.

After installing Java, return to the launcher and select **Check for updates**.

## Updates and files

Each time you launch OpenRTM, the launcher checks the official
[OpenRTM release repository](https://github.com/Avexiis/OpenRTM_Production).
When a newer version is available, **Update & Launch** downloads it before
starting OpenRTM. If GitHub is temporarily unavailable, an already downloaded
version can still be launched.

OpenRTM, its version information, and its settings are kept in:

- Windows: `%USERPROFILE%\.openrtm`
- Linux: `~/.openrtm`

## Remove OpenRTM

Select **Remove** in the launcher to delete OpenRTM, its downloaded JAR, its
settings, and Linux desktop integration. Removal is permanent.

## Command line controls

The graphical launcher opens when no options are supplied. The following
commands are also available for automation or troubleshooting:

```text
--check          Check Java and update availability
--force-update   Download the current OpenRTM JAR again
--no-launch      Update without starting OpenRTM
--install        Install Linux desktop integration
--uninstall      Remove OpenRTM data and desktop integration
--yes            Skip command-line removal confirmation
--help           Show all options
--version        Show the launcher version
```
