#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/BitmapLib.h>
#include <Library/KeypadDeviceLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>

#define KPD_BASE       0x1C00E000
#define KP_MEM1        0x0004
#define KP_DEBOUNCE    0x0018
#define KP_EN          0x0024

#define PMIF_BASE        0x1C804000
#define SPMI_SEND        0x0880  /* SWINF2: the channel pearl's MT6363 answers on */
#define SPMI_RDATA       0x0894
#define SPMI_VLD_CLR     0x08A4
#define SPMI_STA         0x08A8

#define SPMI_FSM_IDLE     0
#define SPMI_FSM_WFVLDCLR 6

#define MT6363_SID        4
#define MT6363_TOPSTATUS  0x1E

#define PMIC_POWER_BIT    BIT1
#define PMIC_HOME_BIT     BIT3

typedef enum {
  KEY_TYPE_VOLUME_UP,
  KEY_TYPE_VOLUME_DOWN,
  KEY_TYPE_POWER,
} KEY_TYPE;

typedef struct {
  KEY_CONTEXT EfiKeyContext;
  KEY_TYPE    Type;
} KEY_CONTEXT_PRIVATE;

UINTN gBitmapScanCodes[BITMAP_NUM_WORDS(0x18)]    = {0};
UINTN gBitmapUnicodeChars[BITMAP_NUM_WORDS(0x7f)] = {0};

EFI_KEY_DATA gKeyDataPowerDown      = {.Key = {.ScanCode = SCAN_RIGHT,}};
EFI_KEY_DATA gKeyDataPowerUp        = {.Key = {.ScanCode = SCAN_LEFT,}};
EFI_KEY_DATA gKeyDataPowerLongpress = {.Key = {.ScanCode = SCAN_ESC,}};

#define MS2NS(ms) (((UINT64)(ms)) * 1000000ULL)

STATIC
inline
VOID
KeySetState (
  UINT16  ScanCode,
  CHAR16  UnicodeChar,
  BOOLEAN Value)
{
  if (ScanCode && ScanCode < 0x18) {
    if (Value) {
      BitmapSet(gBitmapScanCodes, ScanCode);
    } else {
      BitmapClear(gBitmapScanCodes, ScanCode);
    }
  }

  if (UnicodeChar && UnicodeChar < 0x7f) {
    if (Value) {
      BitmapSet(gBitmapUnicodeChars, ScanCode);
    } else {
      BitmapClear(gBitmapUnicodeChars, ScanCode);
    }
  }
}

STATIC
inline
BOOLEAN
KeyGetState (
  UINT16 ScanCode,
  CHAR16 UnicodeChar)
{
  if (ScanCode && ScanCode < 0x18) {
    if (!BitmapTest(gBitmapScanCodes, ScanCode)) {
      return FALSE;
    }
  }

  if (UnicodeChar && UnicodeChar < 0x7f) {
    if (!BitmapTest(gBitmapUnicodeChars, ScanCode)) {
      return FALSE;
    }
  }

  return TRUE;
}

STATIC
inline
VOID
LibKeyInitializeKeyContext (KEY_CONTEXT *Context)
{
  SetMem(&Context->KeyData, sizeof(Context->KeyData), 0);

  Context->Time      = 0;
  Context->State     = KEYSTATE_RELEASED;
  Context->Repeat    = FALSE;
  Context->Longpress = FALSE;
}

STATIC
inline
VOID
LibKeyUpdateKeyStatus (
  KEY_CONTEXT       *Context,
  KEYPAD_RETURN_API *KeypadReturnApi,
  BOOLEAN            IsPressed,
  UINT64             Delta)
{
  // Keep Track of the Actual State
  KeySetState (Context->KeyData.Key.ScanCode, Context->KeyData.Key.UnicodeChar, IsPressed);

  // Update Key Time
  Context->Time += Delta;

  switch (Context->State) {
    case KEYSTATE_RELEASED:
      if (IsPressed) {
        // Change to Pressed
        Context->Time  = 0;
        Context->State = KEYSTATE_PRESSED;
      }

      break;

    case KEYSTATE_PRESSED:
      if (IsPressed) {
        // Key Repeat
        if (Context->Repeat && Context->Time >= MS2NS(100)) {
          KeypadReturnApi->PushEfikeyBufTail (KeypadReturnApi, &Context->KeyData);
          Context->Time   = 0;
          Context->Repeat = TRUE;
        } else if (!Context->Longpress && Context->Time >= MS2NS(500)) {
          // Handle Key Combos
          if (Context->KeyData.Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
            if (KeyGetState (SCAN_DOWN, 0)) {
              // Report SCAN_RIGHT
              KeypadReturnApi->PushEfikeyBufTail (KeypadReturnApi, &gKeyDataPowerDown);
            } else if (KeyGetState (SCAN_UP, 0)) {
              // Report SCAN_LEFT
              KeypadReturnApi->PushEfikeyBufTail (KeypadReturnApi, &gKeyDataPowerUp);
            } else {
              // Report SCAN_ESC
              KeypadReturnApi->PushEfikeyBufTail (KeypadReturnApi, &gKeyDataPowerLongpress);
            }
          } else { // Post first Keyrepeat Event
            // only Start Keyrepeat if we're not doing a Combo
            if (!KeyGetState (0, CHAR_CARRIAGE_RETURN)) {
              KeypadReturnApi->PushEfikeyBufTail (KeypadReturnApi, &Context->KeyData);
              Context->Time = 0;
              // Do not start auto-repeat: a noisy PMIC bit must not flood the
              // console queue while no application is consuming input.
            }
          }

          Context->Longpress = TRUE;
        }
      } else {
        if (!Context->Longpress) {
          // We Supressed Down, so Report it Now
          KeypadReturnApi->PushEfikeyBufTail (KeypadReturnApi, &Context->KeyData);
          Context->State = KEYSTATE_LONGPRESS_RELEASE;
        } else if (Context->Time >= MS2NS(10)) {
          // We Reported another Key Already
          Context->Time      = 0;
          Context->Repeat    = FALSE;
          Context->Longpress = FALSE;
          Context->State     = KEYSTATE_RELEASED;
        }
      }

      break;

    case KEYSTATE_LONGPRESS_RELEASE:
      // Change to Released
      Context->Time      = 0;
      Context->Repeat    = FALSE;
      Context->Longpress = FALSE;
      Context->State     = KEYSTATE_RELEASED;
      break;

    default:
      ASSERT(FALSE);
      break;
  }
}

STATIC KEY_CONTEXT_PRIVATE KeyContextVolumeUp;
STATIC KEY_CONTEXT_PRIVATE KeyContextVolumeDown;
STATIC KEY_CONTEXT_PRIVATE KeyContextPower;
STATIC UINT8               mLastTopStatus = 0xFF;
STATIC UINT8               mStableTopStatus = 0x00;
STATIC UINT8               mCandidateTopStatus = 0x00;
STATIC UINTN               mTopStableCount;
STATIC BOOLEAN             mStableVolumeDown = TRUE;
STATIC BOOLEAN             mCandidateVolumeDown = TRUE;
STATIC UINTN               mVolumeDownStableCount;
STATIC BOOLEAN             mKeysArmed;
STATIC UINTN               mReleasedSamples;

STATIC KEY_CONTEXT_PRIVATE *KeyList[] = { &KeyContextVolumeUp, &KeyContextVolumeDown, &KeyContextPower };

STATIC
UINT32
SpmiFsm (
  VOID
  )
{
  return (MmioRead32 (PMIF_BASE + SPMI_STA) >> 1) & 0x7;
}

STATIC
EFI_STATUS
SpmiReadByte (
  IN  UINT16  Address,
  OUT UINT8   *Value
  )
{
  UINTN   Timeout;
  UINT32  Data;
  UINT32  Command;

  if (SpmiFsm () == SPMI_FSM_WFVLDCLR) {
    MmioWrite32 (PMIF_BASE + SPMI_VLD_CLR, 1);
  }

  for (Timeout = 0; Timeout < 2000; Timeout++) {
    if (SpmiFsm () == SPMI_FSM_IDLE) {
      break;
    }
    MicroSecondDelay (10);
  }
  if (Timeout == 2000) {
    return EFI_TIMEOUT;
  }

  /* bit31:30 = 3 selects the extended-long read the PMIC answers; the
   * SWINF1 short-read form (1 << 30) returns a stale constant on pearl. */
  Command = ((UINT32)3 << 30) | ((UINT32)MT6363_SID << 24) | Address;
  MmioWrite32 (PMIF_BASE + SPMI_SEND, Command);

  for (Timeout = 0; Timeout < 2000; Timeout++) {
    if (SpmiFsm () == SPMI_FSM_WFVLDCLR) {
      break;
    }
    MicroSecondDelay (10);
  }
  if (Timeout == 2000) {
    return EFI_TIMEOUT;
  }

  Data = MmioRead32 (PMIF_BASE + SPMI_RDATA);
  MmioWrite32 (PMIF_BASE + SPMI_VLD_CLR, 1);
  *Value = (UINT8)(Data & 0xFF);
  return EFI_SUCCESS;
}

RETURN_STATUS
EFIAPI
KeypadDeviceConstructor ()
{
  UINTN  Index;

  for (Index = 0; Index < (sizeof (KeyList) / sizeof (KeyList[0])); Index++) {
    ZeroMem (&KeyList[Index]->EfiKeyContext, sizeof (KEY_CONTEXT));
  }

  KeyContextVolumeUp.Type   = KEY_TYPE_VOLUME_UP;
  KeyContextVolumeDown.Type = KEY_TYPE_VOLUME_DOWN;
  KeyContextPower.Type      = KEY_TYPE_POWER;

  //
  // The KPD matrix is already pin-muxed by LK. Enable it and use a short
  // software-friendly debounce; the PMIC keys have their own debounce.
  //
  MmioWrite16 (KPD_BASE + KP_DEBOUNCE, 20);
  MmioWrite16 (KPD_BASE + KP_EN, 1);

  return RETURN_SUCCESS;
}

EFI_STATUS
EFIAPI
KeypadDeviceReset (KEYPAD_DEVICE_PROTOCOL *This)
{
  LibKeyInitializeKeyContext(&KeyContextVolumeUp.EfiKeyContext);
  KeyContextVolumeUp.EfiKeyContext.KeyData.Key.ScanCode   = SCAN_UP;

  LibKeyInitializeKeyContext(&KeyContextVolumeDown.EfiKeyContext);
  KeyContextVolumeDown.EfiKeyContext.KeyData.Key.ScanCode = SCAN_DOWN;

  LibKeyInitializeKeyContext(&KeyContextPower.EfiKeyContext);
  KeyContextPower.EfiKeyContext.KeyData.Key.UnicodeChar   = CHAR_CARRIAGE_RETURN;

  return EFI_SUCCESS;
}

EFI_STATUS
KeypadDeviceGetKeys (
  KEYPAD_DEVICE_PROTOCOL *This,
  KEYPAD_RETURN_API      *KeypadReturnApi,
  UINT64                  Delta)
{
  EFI_STATUS           Status;
  BOOLEAN              IsPressed;
  UINT8                TopStatus;
  UINT8                TopStatus2;
  UINT16               KpdMemory;
  KEY_CONTEXT_PRIVATE *Context;
  UINTN                Index;

  //
  // Sample TOPSTATUS twice and accept it only when both reads agree; the
  // register contains unrelated charger/status bits that make single reads
  // look noisy.  The power and home bits are active-low.
  //
  Status = SpmiReadByte (MT6363_TOPSTATUS, &TopStatus);
  if (!EFI_ERROR (Status) &&
      !EFI_ERROR (SpmiReadByte (MT6363_TOPSTATUS, &TopStatus2)) &&
      (TopStatus == TopStatus2))
  {
    if (TopStatus == mCandidateTopStatus) {
      if (mTopStableCount < 3) {
        mTopStableCount++;
      }
    } else {
      mCandidateTopStatus = TopStatus;
      mTopStableCount     = 1;
    }

    if (mTopStableCount >= 3) {
      mStableTopStatus = mCandidateTopStatus;
    }
  }

  TopStatus = mStableTopStatus;
  KpdMemory = MmioRead16 (KPD_BASE + KP_MEM1);

  if (((KpdMemory & BIT0) == 0) == mCandidateVolumeDown) {
    if (mVolumeDownStableCount < 3) {
      mVolumeDownStableCount++;
    }
  } else {
    mCandidateVolumeDown      = (KpdMemory & BIT0) == 0;
    mVolumeDownStableCount    = 1;
  }

  if (mVolumeDownStableCount >= 3) {
    mStableVolumeDown = mCandidateVolumeDown;
  }

  //
  // Ignore the keys until all of them have been observed released once.
  // The power button is usually held while entering fastboot, and a stale
  // pressed state would otherwise confirm the first Boot Manager entry
  // immediately.
  //
  if (!mKeysArmed) {
    if (((mStableTopStatus & (PMIC_POWER_BIT | PMIC_HOME_BIT)) ==
         (PMIC_POWER_BIT | PMIC_HOME_BIT)) &&
        !mStableVolumeDown)
    {
      if (mReleasedSamples < 100) {
        mReleasedSamples++;
      }
      if (mReleasedSamples >= 100) {
        mKeysArmed = TRUE;
      }
    } else {
      mReleasedSamples = 0;
    }
  }

  for (Index = 0; Index < (sizeof (KeyList) / sizeof (KeyList[0])); Index++) {
    Context = KeyList[Index];

    switch (Context->Type) {
      case KEY_TYPE_VOLUME_UP:
        IsPressed = mKeysArmed && ((TopStatus & PMIC_HOME_BIT) == 0);
        break;
      case KEY_TYPE_VOLUME_DOWN:
        IsPressed = mKeysArmed && mStableVolumeDown;
        break;
      case KEY_TYPE_POWER:
        IsPressed = mKeysArmed && ((TopStatus & PMIC_POWER_BIT) == 0);
        break;
      default:
        continue;
    }

    LibKeyUpdateKeyStatus (&Context->EfiKeyContext, KeypadReturnApi, IsPressed, Delta);
  }

  return EFI_SUCCESS;
}