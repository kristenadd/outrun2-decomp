#pragma once

namespace outrun::platform {

// Port-only switch for the PC's online account sign-in (Xbox Live / netplay).
// The SIGN-IN dialog (key 22, 4DEC60: 4DEFE0 init, 4DF120 control, 4DEEA0 draw)
// is not ported as an overlay, and the online service it talks to is gone.
// While this is false:
//   - the "Not Signed In" / "Signed In: %s" status (441370, texts 0x474 / 0x476)
//     is not drawn,
//   - the SIGN-IN / FRIENDS help-bar slot (key 0x8000, texts 0x298 / 0x297) is
//     left out of the 440EA0 label lists,
//   - X (4536F0 bit 0x10) does not reach the 445810 gate that opens key 22.
// Turn it on once the dialog is ported.
constexpr bool OnlineSignInSupported=false;

}
