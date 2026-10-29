/*  Peakwise - components
 *
 *  Developed by Beyhan KILIC
 *  Copyright (C) 2026 Beyhan KILIC
 *
 *  This program is free software: you can redistribute it and/or modify it under the terms of the
 *  GNU General Public License as published by the Free Software Foundation, either version 3 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 *  even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 *  Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with this program (LICENSE).
 *  If not, see <https://www.gnu.org/licenses/>.
 */

// Components come from the saved interface data (position, size, ranges, parameter flags, defaults);
// the script adds look and feel, CSS variables and the analyser colours.

const var Bg_Pnl          = Content.getComponent("Bg_Pnl");
const var Screen_Pnl      = Content.getComponent("Screen_Pnl");
const var Anls_FT         = Content.getComponent("Anls_FT");
const var Ovr_Pnl         = Content.getComponent("Ovr_Pnl");
const var Header_Pnl      = Content.getComponent("Header_Pnl");
const var PresetPrev_Pnl  = Content.getComponent("PresetPrev_Pnl");
const var Preset_Pnl      = Content.getComponent("Preset_Pnl");
const var PresetNext_Pnl  = Content.getComponent("PresetNext_Pnl");
const var GR_Pnl          = Content.getComponent("GR_Pnl");
const var Info_Pnl        = Content.getComponent("Info_Pnl");
const var Power_Btn       = Content.getComponent("Power_Btn");
const var Listen_Btn      = Content.getComponent("Listen_Btn");
const var Threshold_Knb   = Content.getComponent("Threshold_Knb");
const var Amount_Knb      = Content.getComponent("Amount_Knb");
const var LowLim_Knb      = Content.getComponent("LowLim_Knb");
const var HighLim_Knb     = Content.getComponent("HighLim_Knb");
const var Resize_Pnl      = Content.getComponent("Resize_Pnl");
const var InfoOverlay_Pnl = Content.getComponent("InfoOverlay_Pnl");   // last in the saved order: above everything

Anls_FT.setContentData({
	"Type": "AudioAnalyser",
	"ProcessorId": "Analyser",
	"Index": 2,
	"FollowWorkspace": false,
	"ColourData": { "bgColour": "0x00000000", "itemColour1": "0x14BDBDBD", "itemColour2": "0x40BDBDBD" }
});

Power_Btn.setLocalLookAndFeel(LafPower);
Listen_Btn.setLocalLookAndFeel(LafListen);
pkwsListenLayout(Listen_Btn);

// visible CSS knobs; the range knobs stay hidden parameters, set by the screen selectors
const var pkwsKnobs = [Threshold_Knb, Amount_Knb];
const var kKnobTitles = ["Threshold", "Amount"];

LowLim_Knb.set("visible", false);
HighLim_Knb.set("visible", false);

for (k in pkwsKnobs)
{
	k.setStyleSheetProperty("title", kKnobTitles[pkwsKnobs.indexOf(k)], "");
	k.setLocalLookAndFeel(LafKnob);
	pkwsKnobLayout(k);
}

// re-size a knob's drawing whenever its width / height changes
const var PkwsKnobSize = Engine.createBroadcaster({ "id": "knob size", "args": ["component", "property", "value"] });

PkwsKnobSize.addListener("", "knob geometry", function(component, property, value)
{
	pkwsKnobLayout(component);
});

PkwsKnobSize.attachToComponentProperties(["Threshold_Knb", "Amount_Knb"], ["width", "height"], "knob size");

// the info panel always starts closed
InfoOverlay_Pnl.set("visible", false);
