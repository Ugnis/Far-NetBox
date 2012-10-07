//---------------------------------------------------------------------------
#ifndef WinSCPPluginH
#define WinSCPPluginH
//---------------------------------------------------------------------------
#include <Interface.h>
#include "FarPlugin.h"
//---------------------------------------------------------------------------
class TWinSCPFileSystem;
class TCopyParamType;
//---------------------------------------------------------------------------
struct TMessageParams
{
  TMessageParams();

  const TQueryButtonAlias * Aliases;
  int AliasesCount;
  unsigned int Flags;
  unsigned int Params;
  unsigned int Timer;
  TQueryParamsTimerEvent TimerEvent;
  UnicodeString TimerMessage;
  unsigned int TimerAnswers;
  unsigned int Timeout;
  unsigned int TimeoutAnswer;
};
//---------------------------------------------------------------------------
class TWinSCPPlugin : public TCustomFarPlugin
{
  friend TWinSCPFileSystem;
public:
  explicit TWinSCPPlugin(HINSTANCE HInst);
  virtual ~TWinSCPPlugin();
  virtual intptr_t __fastcall GetMinFarVersion();

  virtual void __fastcall HandleException(Exception * E, int OpMode = 0);
  int __fastcall MoreMessageDialog(UnicodeString Str, TStrings * MoreMessages,
    TQueryType Type, int Answers, const TMessageParams * Params = NULL);
  void __fastcall ShowExtendedException(Exception * E);
  bool __fastcall CopyParamCustomDialog(TCopyParamType & CopyParam,
    int CopyParamAttrs);
  virtual void __fastcall SetStartupInfo(const struct PluginStartupInfo * Info);

protected:
  virtual bool __fastcall HandlesFunction(THandlesFunction Function);
  virtual void __fastcall GetPluginInfoEx(DWORD & Flags, TStrings * DiskMenuStrings,
    TStrings * PluginMenuStrings, TStrings * PluginConfigStrings,
    TStrings * CommandPrefixes);
  virtual TCustomFarFileSystem * __fastcall OpenPluginEx(intptr_t OpenFrom, intptr_t Item);
  virtual bool __fastcall ConfigureEx(intptr_t Item);
  virtual intptr_t __fastcall ProcessEditorEventEx(intptr_t Event, void * Param);
  virtual intptr_t __fastcall ProcessEditorInputEx(const INPUT_RECORD * Rec);
  bool __fastcall CopyParamDialog(UnicodeString Caption, TCopyParamType & CopyParam,
    int CopyParamAttrs);
  void MessageClick(void * Token, int Result, bool & Close);

  void __fastcall CommandsMenu(bool FromFileSystem);
  bool __fastcall ConfigurationDialog();
  bool __fastcall PanelConfigurationDialog();
  bool __fastcall TransferConfigurationDialog();
  bool __fastcall QueueConfigurationDialog();
  bool __fastcall EnduranceConfigurationDialog();
  bool __fastcall TransferEditorConfigurationDialog();
  bool __fastcall LoggingConfigurationDialog();
  bool __fastcall ConfirmationsConfigurationDialog();
  bool __fastcall IntegrationConfigurationDialog();
  void __fastcall AboutDialog();

private:
  bool FInitialized;
  TWinSCPPlugin * Self;
};
//---------------------------------------------------------------------------
#endif
