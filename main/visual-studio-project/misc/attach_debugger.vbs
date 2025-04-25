Option Explicit ' Enforces variable declaration for better debugging

' Declare variables
Dim objShell, objWmi, objFSO, objFile, colProcesses, objProcess
Dim godotPath, godotExecutable, projectPath, debuggerPath, line, configDict
Dim mainScene, processFound, maxAttempts, attempt

' Initialize objects
Set objShell = CreateObject("WScript.Shell")
Set objWmi = GetObject("winmgmts://./root/cimv2")
Set objFSO = CreateObject("Scripting.FileSystemObject")
Set configDict = CreateObject("Scripting.Dictionary")

' Read config file
If objFSO.FileExists("config.txt") Then
    Set objFile = objFSO.OpenTextFile("config.txt", 1) ' 1 = read-only
    Do Until objFile.AtEndOfStream
        line = Trim(objFile.ReadLine)
        If InStr(line, "=") > 0 Then
            configDict.Add Split(line, "=")(0), Split(line, "=")(1)
        End If
    Loop
    objFile.Close
Else
    WScript.Echo "Error: config.txt not found!"
    WScript.Quit 1
End If

' Assign config values
godotPath = configDict("GODOT_PATH")
godotExecutable = configDict("GODOT_EXECUTABLE")
projectPath = configDict("PROJECT_PATH")
debuggerPath = configDict("DEBUGGER_PATH")

' Validate config values
If godotPath = "" Or godotExecutable = "" Or projectPath = "" Or debuggerPath = "" Then
    WScript.Echo "Error: Missing required config values in config.txt!"
    WScript.Quit 1
End If

' Read main scene from project.godot
Dim projectGodotPath
projectGodotPath = projectPath & "\project.godot"
If Not objFSO.FileExists(projectGodotPath) Then
    WScript.Echo "Error: project.godot not found in project path!"
    WScript.Quit 1
End If

Set objFile = objFSO.OpenTextFile(projectGodotPath, 1)
mainScene = ""
Do Until objFile.AtEndOfStream
    line = Trim(objFile.ReadLine)
    If InStr(line, "run/main_scene=") = 1 Then
        mainScene = Mid(line, Len("run/main_scene=") + 1)
        mainScene = Replace(mainScene, """", "") ' Remove quotes
        Exit Do
    End If
Loop
objFile.Close

If mainScene = "" Then
    WScript.Echo "Error: Could not find run/main_scene in project.godot!"
    WScript.Quit 1
End If

' Check if Godot process already exists
processFound = False
Set colProcesses = objWmi.ExecQuery("Select * from Win32_Process Where Name = '" & godotExecutable & "'")
For Each objProcess in colProcesses
    If InStr(objProcess.CommandLine, mainScene) > 0 Then
        processFound = True
        WScript.Sleep 1000 ' Wait 1 second
        ' Attach debugger to existing process
        objShell.Run """" & debuggerPath & """ -p " & objProcess.ProcessId, 0, False
        Exit For
    End If
Next

' If no existing process found, launch Godot and wait for it to start
If Not processFound Then
    ' Launch Godot with the main scene
    objShell.Run """" & godotPath & "\" & godotExecutable & """ --path """ & projectPath & """ """ & mainScene & """", 1, False

    ' Wait for Godot to start (up to 10 seconds)
    maxAttempts = 20 ' Check 20 times (10 seconds with 500ms intervals)
    attempt = 0
    Do While attempt < maxAttempts And Not processFound
        WScript.Sleep 500 ' Wait 0.5 seconds per check
        Set colProcesses = objWmi.ExecQuery("Select * from Win32_Process Where Name = '" & godotExecutable & "'")
        For Each objProcess in colProcesses
            If InStr(objProcess.CommandLine, mainScene) > 0 Then
                processFound = True
                WScript.Sleep 1000 ' Wait 1 second
                ' Attach debugger
                objShell.Run """" & debuggerPath & """ -p " & objProcess.ProcessId, 0, False
                Exit For
            End If
        Next
        attempt = attempt + 1
    Loop

    ' Check if Godot was found
    If Not processFound Then
        WScript.Echo "Error: Could not find Godot process after " & maxAttempts * 0.5 & " seconds!"
        WScript.Quit 1
    End If
End If