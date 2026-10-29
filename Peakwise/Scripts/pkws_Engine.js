/*  Peakwise - DSP binding
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

// Node parameters: 0 Threshold, 1 Amount, 2 Attack, 3 Release, 4 Workers, 5 LowLim, 6 HighLim, 7 Hear.

const var SupFX = Synth.getEffect("Supressor");

const var kMeterMaxDb = 48.0;      // meter full scale = the node's meter range
const var kMinRatio   = 1.3;       // narrowest high / low window

// [0] low Hz  [1] high Hz  [2] power  [3] listen  (readable from paint / timer callbacks)
const var pkwsSt = Buffer.create(4);

inline function pkwsApplyState()
{
	SupFX.setAttribute(2, 22.0);
	SupFX.setAttribute(3, 92.0);
	SupFX.setAttribute(4, 8.0);
	SupFX.setAttribute(5, LowLim_Knb.getValue());
	SupFX.setAttribute(6, HighLim_Knb.getValue());
	SupFX.setAttribute(7, Listen_Btn.getValue() >= 0.5 ? 1.0 : 0.0);
	SupFX.setAttribute(1, Amount_Knb.getValue());
	SupFX.setAttribute(0, Threshold_Knb.getValue());
	SupFX.setBypassed(1 - Power_Btn.getValue());

	pkwsSt[0] = LowLim_Knb.getValue();
	pkwsSt[1] = HighLim_Knb.getValue();
	pkwsSt[2] = Power_Btn.getValue();
	pkwsSt[3] = Listen_Btn.getValue();
}

// every value change (user, automation, preset, start-up restore) pushes the full state and the knob arc
const var PkwsSync = Engine.createBroadcaster({ "id": "state", "args": ["component", "value"] });

PkwsSync.addListener("engine", "push state", function(component, value)
{
	pkwsApplyState();

	if (pkwsKnobs.indexOf(component) != -1)
		pkwsKnobUpdate(component);
});

PkwsSync.attachToComponentValue(["Power_Btn", "Listen_Btn", "Threshold_Knb", "Amount_Knb", "LowLim_Knb", "HighLim_Knb"], "state");
