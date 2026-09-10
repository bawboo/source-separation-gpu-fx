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
; The CUDA runtime is split into a size-dependent number of archives, so the
; builder generates one [Files] line per archive into this include file.
#ifndef CudaRuntimeFilesIss
  #error CudaRuntimeFilesIss is required.
#endif
#ifndef OutputDirectory
  #define OutputDirectory ".\Output"
#endif

#define AppName "Music SSP FX"
#define AppExeName "Music SSP FX.exe"
#define ModelFile "955717e8-8726e21a.th"
#define ModelUrl "https://dl.fbaipublicfiles.com/demucs/hybrid_transformer/955717e8-8726e21a.th"
#define ModelBytes "84141911"
#define ModelSha256 "8726e21a993978c7ba086d3872e7608d7d5bfca646ca4aca459ffda844faa8b4"

[Setup]
AppId={{E73B2AB8-B8D1-4AD7-B2B9-1BDFEC57E77C}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Music SSP FX contributors
; Also the AGPL corresponding-source location for these binaries.
AppPublisherURL=https://github.com/bawboo/source-separation-gpu-fx
AppSupportURL=https://github.com/bawboo/source-separation-gpu-fx/issues
AppUpdatesURL=https://github.com/bawboo/source-separation-gpu-fx/releases
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDirectory}
OutputBaseFilename=Music_SSP_FX_Setup_x64
SetupIconFile={#PayloadRoot}\Music SSP FX.ico
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
#include CudaRuntimeFilesIss

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
var
  Detected: String;
begin
  RunHardwareProbe;
  { The probe result belongs in the SubCaption, which is laid out when the page
    is created. Adding a CheckListBox group row instead would shift the radio
    indices that SelectedValueIndex reads. }
  if CudaUsable then
    Detected := '偵測結果：找到可用的 NVIDIA CUDA 顯示卡，自動模式會裝 GPU 版。'
  else if NvidiaPresent then
    Detected := '偵測結果：有 NVIDIA 顯示卡，但 CUDA 驅動目前不可用（建議更新顯示卡驅動）；' +
      '自動模式會裝 CPU 版。'
  else
    Detected := '偵測結果：沒有可用的 NVIDIA CUDA 顯示卡，自動模式會裝 CPU 版。';

  RuntimePage := CreateInputOptionPage(
    wpSelectDir,
    '運算 runtime / Compute runtime',
    '選擇要安裝的 PyTorch runtime / Select the PyTorch runtime to install',
    '安裝程式只會下載你選擇的那一種，之後也可以重新執行安裝程式換成另一種。' + #13#10 +
    'Setup downloads and installs only the selected runtime.' + #13#10 + #13#10 +
    Detected,
    True,
    False);
  { Radio captions do not word-wrap, so each option stays on a single line. }
  RuntimePage.Add('自動偵測（建議）／Automatic — 有 CUDA 顯示卡就裝 GPU 版，否則裝 CPU 版');
  RuntimePage.Add('CPU — 下載約 300 MB，任何電腦都能跑，分離較慢');
  RuntimePage.Add('GPU (CUDA) — 需 NVIDIA 顯示卡，下載約 2.5 GB，分離快很多');
  RuntimePage.SelectedValueIndex := 0;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  { Choosing CUDA by hand on a machine without a usable one downloads ~2.5 GB
    that cannot run, so make the user confirm rather than silently obeying. }
  if Assigned(RuntimePage) and (CurPageID = RuntimePage.ID) and
     (RuntimePage.SelectedValueIndex = 2) and not CudaUsable then
    Result := MsgBox(
      '這台電腦目前偵測不到可用的 NVIDIA CUDA 顯示卡。' + #13#10 +
      '仍要下載 GPU (CUDA) 版嗎？它會多下載約 2.5 GB，且可能無法啟動。' + #13#10 + #13#10 +
      'No usable NVIDIA CUDA GPU was detected. Install the CUDA runtime anyway?',
      mbConfirmation,
      MB_YESNO) = IDYES;
end;

function InstallCudaRuntime: Boolean;
var
  Forced: String;
begin
  { /RUNTIME=cpu|cuda|auto for silent and scripted installs. }
  Forced := Lowercase(ExpandConstant('{param:RUNTIME|auto}'));
  if Forced = 'cpu' then
  begin
    Result := False;
    Exit;
  end;
  if Forced = 'cuda' then
  begin
    Result := True;
    Exit;
  end;
  if not Assigned(RuntimePage) then
  begin
    Result := CudaUsable;
    Exit;
  end;
  case RuntimePage.SelectedValueIndex of
    1: Result := False;
    2: Result := True;
  else
    Result := CudaUsable;
  end;
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
  if CurPageID <> wpReady then
    Exit;
  if InstallCudaRuntime then
    WizardForm.ReadyMemo.Lines.Add(
      '將安裝 GPU (CUDA) runtime / Installing the CUDA runtime.')
  else if NvidiaPresent and not CudaUsable then
    WizardForm.ReadyMemo.Lines.Add(
      '偵測到 NVIDIA 顯示卡但 CUDA 驅動無法使用，將安裝 CPU runtime.' + #13#10 +
      'An NVIDIA GPU was detected, but the CUDA Driver API is unavailable. Setup will install the CPU runtime.')
  else
    WizardForm.ReadyMemo.Lines.Add(
      '將安裝 CPU runtime / Installing the CPU runtime.');
end;

{ Runs the worker's self-test with its output captured to LogPath, so a
  failure has a reason the user can read (the bare exit code told nobody
  anything). Returns True when the worker reported success. }
function RunSelfTest(const WorkerPath, ModelPath, ReportPath, LogPath, Device: String;
  var ExitCode: Integer): Boolean;
var
  CommandLine: String;
begin
  DeleteFile(ReportPath);
  CommandLine := '/C ""' + WorkerPath + '" --self-test-json "' + ReportPath +
    '" --models-dir "' + ModelPath + '" --model htdemucs --device ' + Device +
    ' > "' + LogPath + '" 2>&1"';
  Result := Exec(ExpandConstant('{cmd}'), CommandLine, '', SW_HIDE,
    ewWaitUntilTerminated, ExitCode) and (ExitCode = 0);
end;

{ The reason a self-test failed: the "error" field of the report when the
  worker got far enough to write one, otherwise the last lines it printed. }
function SelfTestReason(const ReportPath, LogPath: String; ExitCode: Integer): String;
var
  Document: AnsiString;
  Lines: TArrayOfString;
  Index, Start, Finish, Shown: Integer;
begin
  Result := '';
  if LoadStringFromFile(ReportPath, Document) and (Pos('"status": "fail"', Document) > 0) then
  begin
    Start := Pos('"error": "', Document);
    if Start > 0 then
    begin
      Start := Start + Length('"error": "');
      Finish := Start;
      while (Finish <= Length(Document)) and (Document[Finish] <> #34) do
        Finish := Finish + 1;
      Result := String(Copy(Document, Start, Finish - Start));
    end;
  end;
  if (Result = '') and LoadStringsFromFile(LogPath, Lines) then
  begin
    Shown := 0;
    Index := GetArrayLength(Lines) - 1;
    while (Index >= 0) and (Shown < 4) do
    begin
      if Trim(Lines[Index]) <> '' then
      begin
        if Result = '' then
          Result := Trim(Lines[Index])
        else
          Result := Trim(Lines[Index]) + #13#10 + Result;
        Shown := Shown + 1;
      end;
      Index := Index - 1;
    end;
  end;
  if Result = '' then
    Result := 'the worker produced no output (exit code ' + IntToStr(ExitCode) +
      '); it may have been blocked by antivirus software, or the runtime files are incomplete';
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  WorkerPath: String;
  ModelPath: String;
  ReportPath: String;
  LogPath: String;
  Device: String;
  Reason: String;
  ExitCode: Integer;
  Passed: Boolean;
begin
  if CurStep = ssInstall then
  begin
    // The runtime archive is extracted over the app directory. A runtime
    // left by an earlier install of the other flavour (a CUDA torch under a
    // CPU one, or the reverse) leaves DLLs the new torch then tries to load
    // and fails on (WinError 127 on c10_cuda.dll) - which is exactly how the
    // 0.0.5 self-test failed. Start from an empty runtime directory.
    DelTree(ExpandConstant('{app}\Resources\sidecar\Runtime'), True, True, True);
    Exit;
  end;
  if CurStep <> ssPostInstall then
    Exit;
  WorkerPath := ExpandConstant(
    '{app}\Resources\sidecar\Runtime\htdemucs-worker\htdemucs-worker.exe');
  ModelPath := ExpandConstant('{localappdata}\{#AppName}\Models');
  ReportPath := ExpandConstant('{localappdata}\{#AppName}\Logs\install-self-test.json');
  LogPath := ExpandConstant('{localappdata}\{#AppName}\Logs\install-self-test.log');
  ForceDirectories(ExtractFileDir(ReportPath));
  { /SELFTESTDEVICE=cpu|cuda:0 overrides the device for diagnosis. }
  Device := ExpandConstant('{param:SELFTESTDEVICE|' + SelectedDeviceArgument + '}');
  Passed := RunSelfTest(WorkerPath, ModelPath, ReportPath, LogPath, Device, ExitCode);
  if (not Passed) and (Device <> 'cpu') then
  begin
    { A GPU that PyTorch cannot use (driver too old, no memory, a laptop
      switching adapters) must not brick the installation: the CUDA runtime
      runs on the CPU as well, and the app picks the CPU by itself. }
    Reason := SelfTestReason(ReportPath, LogPath, ExitCode);
    Passed := RunSelfTest(WorkerPath, ModelPath, ReportPath, LogPath, 'cpu', ExitCode);
    if Passed then
      MsgBox(
        'GPU 自我測試失敗，但 CPU 自我測試通過；App 會先用 CPU 運算。' + #13#10 +
        '更新 NVIDIA 驅動程式後重新執行安裝程式即可改用 GPU。' + #13#10 + #13#10 +
        'The GPU self-test failed but the CPU self-test passed; the app will run on the CPU.' + #13#10 +
        'Update the NVIDIA driver and run Setup again to use the GPU.' + #13#10 + #13#10 +
        'GPU error: ' + Reason + #13#10 +
        'Log: ' + LogPath,
        mbInformation, MB_OK);
  end;
  if not Passed then
  begin
    Reason := SelfTestReason(ReportPath, LogPath, ExitCode);
    if MsgBox(
        '安裝後的自我測試失敗 / The post-install self-test failed:' + #13#10 +
        Reason + #13#10 + #13#10 +
        '詳細記錄 / Details: ' + LogPath + #13#10 + #13#10 +
        '仍要完成安裝嗎？（問題解決前 App 可能無法分離。）' + #13#10 +
        'Finish the installation anyway? (The app may not separate until this is resolved.)',
        mbError, MB_YESNO) = IDNO then
      RaiseException(
        'The post-install PyTorch/HTDemucs self-test failed: ' + Reason);
  end;
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
