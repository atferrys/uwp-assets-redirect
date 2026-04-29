# UWP Assets Redirect
Replace UWP app assets (such as icons) without worrying about updates
or modifying system files permissions.

## Example: Before and After
![Before and after comparison of some applications](https://raw.githubusercontent.com/atferrys/uwp-assets-redirect/main/docs-assets/example-before-after.png)

# Finding the Application bundle and assets
You can quickly identify both the application bundle and its Assets folder using Task Manager.

1. Open the application you want to redirect assets for. Then open Task Manager,
right-click the application process, and select “**Open file location**”:
   
    ![Task Manager right-click context menu on "Microsoft Store" with "Open file location" hovered](https://raw.githubusercontent.com/atferrys/uwp-assets-redirect/main/docs-assets/task-manager-open-location.png)

2. A File Explorer window will open with the application’s folder highlighted:
   
    ![Application folder highlighted in File Explorer with the bundle identifier shown](https://raw.githubusercontent.com/atferrys/uwp-assets-redirect/main/docs-assets/application-folder.png)

3. The application bundle is the part of the folder name that comes before the first underscore,
in this case `Microsoft.WindowsStore`.

If Assets Redirect can’t automatically locate the Assets folder, you can browse the application directory to manually find it.
In this example, although it was detected automatically, the assets were located in `Assets\AppTiles`:

![The path to the assets folder](https://raw.githubusercontent.com/atferrys/uwp-assets-redirect/main/docs-assets/assets-folder.png)

You can specify them in the application bundle using this format:
`<application bundle>`|`<assets folder>`, and in this case `Microsoft.WindowsStore|Assets\AppTiles`.

# Creating custom assets
You can manually create replacement assets by copying the original Assets folder, removing any files you don’t
want to replace, and editing the remaining ones making sure to preserve the original file resolutions.

A quicker and easier approach is to use something like [TileGen](https://tilegen.ferrys.it/assets-redirect), an open-source tool that
generates the required assets from a single image or an `.ico` file.
_Using an .ico file is recommended, as it already contains multiple resolutions._

# Theme paths
Theme paths can be set in the settings.
Each theme path can be a folder with custom assets files and a `theme.ini` file that contains redirection rules, or the `.ini` theme file itself.

For example, the `theme.ini` file may contain the following redirection rules:

## WindowsApps and SystemApps redirections
For apps found in `C:\Program Files\WindowsApps` and in `C:\Windows\SystemApps`,
you can use respectively the `[windows-apps]` and `[system-apps]` headers.

Each rule should be provided in this format: `<application bundle>`=`<redirection folder>`.
The application bundle can be easily found by following [the guide above](#finding-the-application-bundle-and-assets).

### Example config
```
[windows-apps]
Microsoft.WindowsStore=.\Microsoft Store
Microsoft.WindowsCalculator=.\Calculator
```

Most of the time, Assets Redirect can automatically locate the bundle’s Assets folder.
However, some applications use the same application bundle as other apps, which can prevent Assets Redirect
from identifying the correct folder.
In these cases, you can manually specify the Assets folder within the application bundle using the
following format: `<application bundle>`|`<assets folder>` (an example is shown below in `system-apps`).

### Example config
```
[system-apps]
MicrosoftWindows.Client.CBS|WindowsBackup\Assets=.\Windows Backup
Microsoft.PPIProjection=.\Wireless Display
```

## Custom redirections
For apps that aren't found in common folders like `WindowsApps` or `SystemApps`,
like _Settings_, you can use the `[custom]` header.

Each rule should be provided in this format: `<assets folder>`=`<redirection folder>`.

### Example config
```
[custom]
%SystemRoot%\ImmersiveControlPanel\images=.\Settings
```

# Injecting into other processes
By default, Assets Redirect only targets the processes that most commonly use these assets, such as File Explorer and the Start Menu.
As a result, UWP applications are not affected by asset redirection out of the box.

You can change this behavior using the “Custom process inclusion list” in the Advanced tab and doing one of the following:
- Include a specific application’s executable name or path like `WinStore.App.exe` (you can find this in the Details tab of Task Manager).
- Include the entire applications directories: `C:\Program Files\WindowsApps\*` and `C:\Windows\SystemApps\*`.
- Or, if you’re willing to risk system stability, include all processes using `*`.

Doing this applies your asset changes not only to the Windows shell,
but also to the applications themselves, changing their look as well (like the splash screen).

# Planned features
- Downloadable themes like in Resource Redirect, Taskbar Styler, Notification Center Styler, and others.