#if VER < EncodeVer(6,7,0)
  #error Inno Setup 6.7 or newer is required for verified download and archive extraction.
#endif

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef PayloadRoot
  #error PayloadRoot must point to the staged installer payload.
#endif
#ifndef CpuRuntimeUrl
  #error CpuRuntimeUrl is required.
#endif
#ifndef CpuRuntimeBytes
  #error CpuRuntimeBytes is required.
#endif
#ifndef CpuRuntimeSha256
  #error CpuRuntimeSha256 is required.
#endif
#ifndef CudaCoreRuntimeUrl
  #error CudaCoreRuntimeUrl is required.
#endif
#ifndef CudaCoreRuntimeBytes
  #error CudaCoreRuntimeBytes is required.
#endif
#ifndef CudaCoreRuntimeSha256
  #error CudaCoreRuntimeSha256 is required.
#endif
#ifndef CudaLibrariesRuntimeUrl
  #error CudaLibrariesRuntimeUrl is required.
#endif
#ifndef CudaLibrariesRuntimeBytes
  #error CudaLibrariesRuntimeBytes is required.
#endif
#ifndef CudaLibrariesRuntimeSha256
  #error CudaLibrariesRuntimeSha256 is required.
#endif
#ifndef OutputDirectory
  #define OutputDirectory ".\Output"
#endif

#define AppName "HTDemucs GPU FX"
#define AppExeName "HTDemucs GPU FX.exe"
#define ModelFile "955717e8-8726e21a.th"
#define ModelUrl "https://dl.fbaipublicfiles.com/demucs/hybrid_transformer/955717e8-8726e21a.th"
#define ModelBytes "84141911"
#define ModelSha256 "8726e21a993978c7ba086d3872e7608d7d5bfca646ca4aca459ffda844faa8b4"

[Setup]
AppId={{E73B2AB8-B8D1-4AD7-B2B9-1BDFEC57E77C}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=HTDemucs GPU FX contributors
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDirectory}
OutputBaseFilename=HTDemucs_GPU_FX_Setup_x64
SetupIconFile={#PayloadRoot}\HTDemucs GPU FX.ico
Compression=lzma2/max
SolidCompression=yes
ArchiveExtraction=full
WizardStyle=modern
UninstallDisplayIcon={app}\{#AppExeName}
AppMutex=HTDemucsGpuFX.Application,HTDemucsGpuFX.Installer
CloseApplications=yes
RestartApplications=no
ChangesEnvironment=no
MinVersion=10.0.19045

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#PayloadRoot}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PayloadRoot}\htfx_hardware_probe.exe"; Flags: dontcopy

; Runtime archives contain a Resources tree and are extracted directly into {app}.
Source: "{#CpuRuntimeUrl}"; DestDir: "{app}"; DestName: "runtime-win-x64-cpu.zip"; ExternalSize: {#CpuRuntimeBytes}; Hash: "{#CpuRuntimeSha256}"; Flags: external download extractarchive ignoreversion recursesubdirs createallsubdirs; Check: InstallCpuRuntime
Source: "{#CudaCoreRuntimeUrl}"; DestDir: "{app}"; DestName: "runtime-win-x64-cuda-core.zip"; ExternalSize: {#CudaCoreRuntimeBytes}; Hash: "{#CudaCoreRuntimeSha256}"; Flags: external download extractarchive ignoreversion recursesubdirs createallsubdirs; Check: InstallCudaRuntime
Source: "{#CudaLibrariesRuntimeUrl}"; DestDir: "{app}"; DestName: "runtime-win-x64-cuda-libraries.zip"; ExternalSize: {#CudaLibrariesRuntimeBytes}; Hash: "{#CudaLibrariesRuntimeSha256}"; Flags: external download extractarchive ignoreversion recursesubdirs createallsubdirs; Check: InstallCudaRuntime

; Model metadata lives with per-user weights and survives uninstall/reinstall.
Source: "{#PayloadRoot}\Resources\sidecar\models\model-manifest.json"; DestDir: "{localappdata}\{#AppName}\Models"; Flags: ignoreversion uninsneveruninstall
Source: "{#PayloadRoot}\Resources\sidecar\models\*.yaml"; DestDir: "{localappdata}\{#AppName}\Models"; Flags: ignoreversion uninsneveruninstall
Source: "{#ModelUrl}"; DestDir: "{localappdata}\{#AppName}\Models"; DestName: "{#ModelFile}"; ExternalSize: {#ModelBytes}; Hash: "{#ModelSha256}"; Flags: external download ignoreversion uninsneveruninstall

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional tasks:"; Flags: unchecked

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

[Code]
var
  RuntimePage: TInputOptionWizardPage;
  CudaUsable: Boolean;
  NvidiaPresent: Boolean;
  ProbeDocument: AnsiString;

function JsonBoolean(const Document, Name: AnsiString): Boolean;
begin
  Result := Pos('"' + Name + '": true', Document) > 0;
end;

procedure RunHardwareProbe;
var
  ProbePath: String;
  ReportPath: String;
  ExitCode: Integer;
begin
  CudaUsable := False;
  NvidiaPresent := False;
  ExtractTemporaryFile('htfx_hardware_probe.exe');
  ProbePath := ExpandConstant('{tmp}\htfx_hardware_probe.exe');
  ReportPath := ExpandConstant('{tmp}\htfx-hardware.json');
  if Exec(ProbePath, '--json "' + ReportPath + '"', '', SW_HIDE,
      ewWaitUntilTerminated, ExitCode) and (ExitCode = 0) and
      LoadStringFromFile(ReportPath, ProbeDocument) then
  begin
    CudaUsable := JsonBoolean(ProbeDocument, 'cuda_usable');
    NvidiaPresent := JsonBoolean(ProbeDocument, 'nvidia_adapter_present');
  end;
end;

procedure InitializeWizard;
begin
  RunHardwareProbe;
  RuntimePage := CreateInputOptionPage(
    wpSelectDir,
    'Compute runtime',
    'Select the PyTorch runtime to install',
    'Setup downloads and installs only the selected runtime.',
    True,
    False);
  RuntimePage.Add('NVIDIA CUDA GPU (recommended)');
  RuntimePage.Add('CPU only');
  if CudaUsable then
    RuntimePage.SelectedValueIndex := 0
  else
    RuntimePage.SelectedValueIndex := 1;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := Assigned(RuntimePage) and (PageID = RuntimePage.ID) and not CudaUsable;
end;

function InstallCudaRuntime: Boolean;
begin
  Result := CudaUsable and Assigned(RuntimePage) and
    (RuntimePage.SelectedValueIndex = 0);
end;

function InstallCpuRuntime: Boolean;
begin
  Result := not InstallCudaRuntime;
end;

function SelectedDeviceArgument: String;
begin
  if InstallCudaRuntime then
    Result := 'cuda:0'
  else
    Result := 'cpu';
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpReady) and not CudaUsable then
  begin
    if NvidiaPresent then
      WizardForm.ReadyMemo.Lines.Add(
        'An NVIDIA GPU was detected, but the CUDA Driver API is unavailable. Setup will install the CPU runtime.')
    else
      WizardForm.ReadyMemo.Lines.Add(
        'No usable NVIDIA CUDA GPU was detected. Setup will install the CPU runtime.');
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  WorkerPath: String;
  ModelPath: String;
  ReportPath: String;
  Arguments: String;
  ExitCode: Integer;
begin
  if CurStep <> ssPostInstall then
    Exit;
  WorkerPath := ExpandConstant(
    '{app}\Resources\sidecar\Runtime\htdemucs-worker\htdemucs-worker.exe');
  ModelPath := ExpandConstant('{localappdata}\{#AppName}\Models');
  ReportPath := ExpandConstant('{localappdata}\{#AppName}\Logs\install-self-test.json');
  ForceDirectories(ExtractFileDir(ReportPath));
  Arguments := '--self-test-json "' + ReportPath + '" --models-dir "' +
    ModelPath + '" --model htdemucs --device ' + SelectedDeviceArgument;
  if not Exec(WorkerPath, Arguments, '', SW_HIDE, ewWaitUntilTerminated, ExitCode) or
      (ExitCode <> 0) then
    RaiseException(
      'The post-install PyTorch/HTDemucs self-test failed. Setup will not be marked as successful.');
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    if MsgBox(
      'Also delete downloaded models, settings, cache, and local logs?' + #13#10 +
      'Exported audio and video files will not be deleted.',
      mbConfirmation,
      MB_YESNO) = IDYES then
      DelTree(ExpandConstant('{localappdata}\{#AppName}'), True, True, True);
  end;
end;
