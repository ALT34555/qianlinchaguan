' ===========================================================================
'  Qianlin Chaguan - silent launcher (no console window).
'  Starts the local game server in the background and opens the browser.
'  All output is appended to platforms\windows\launcher.log for troubleshooting.
'  To stop it later, run the "stop local service" .cmd in this folder
'  (equivalently: node platforms\windows\launcher.mjs --stop).
' ===========================================================================
Option Explicit
Dim shell, fso, here, root, logFile, command
Set shell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")

here = fso.GetParentFolderName(WScript.ScriptFullName)
root = fso.GetParentFolderName(fso.GetParentFolderName(here))
logFile = here & "\launcher.log"

' Keep the log from growing without bound.
If fso.FileExists(logFile) Then
  If fso.GetFile(logFile).Size > 1048576 Then fso.DeleteFile logFile, True
End If

shell.CurrentDirectory = root
command = "cmd /c npm run desktop >> """ & logFile & """ 2>&1"
shell.Run command, 0, False
