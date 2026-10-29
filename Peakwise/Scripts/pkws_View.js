/*  Peakwise - screen, reduction curve, header, presets
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

// Reduction data: the node's SliderPack 0, log-spaced 50 Hz .. 16 kHz, 0..1 of kMeterMaxDb.
// Frequency axis = HISE's analyser axis: x = (w - 5) * log(f / 20) / log(1000) + 2.5.

const var SupPack = Synth.getSliderPackProcessor("Supressor").getSliderPack(0);
SupPack.setNumSliders(128);
const var kBins = SupPack.getNumSliders();

const var pkwsLive = Buffer.create(kBins);    // eased in time
const var pkwsDraw = Buffer.create(kBins);    // and smoothed across frequency
const var pkwsDisp = Buffer.create(2);        // [0] repaint key  [1] peak dB shown
const var pkwsFill = Content.createPath();
const var pkwsLine = Content.createPath();

const var kGridHz    = [50, 100, 200, 500, 1000, 2000, 5000, 10000];
const var kGridLabel = ["50", "100", "200", "500", "1k", "2k", "5k", "10k"];

inline function pkwsX(hz, w)  { return (w - 5.0) * Math.log(hz / 20.0) / Math.log(1000.0) + 2.5; }
inline function pkwsHzAt(x, w) { return 20.0 * Math.pow(1000.0, (x - 2.5) / (w - 5.0)); }
inline function pkwsBinHz(i)  { return 50.0 * Math.pow(320.0, i / (kBins - 1.0)); }

Bg_Pnl.setPaintRoutine(function(g) { g.fillAll(C_BLACK); });
Screen_Pnl.setPaintRoutine(function(g) { g.fillAll(C_SCREEN); });

// "Peakwise" (Lato Bold 23 px, spacing 0.04) as an outline path, so hover scaling is smooth (drawn text snaps
// to the pixel grid). Generated from Images/Fonts/Lato-Bold.ttf; regenerate if the title changes.
const var kTitlePathData = "4260.t0ltABdPdDE0AwltABdPdb09AwFIhubPdb09AwFIhubPiqGiAw1T9+dPiqGiAE2LbrePiqGiAEkmAHjLV7XPwgnqEHDfwFYPmNEBBsqWVFTbFiuBBY+BaFjm7vfP7.YnAEWc.1fPARApAUGfMHTyR+ZPwUGfMHDmeeaP8iBCBIOk9FTbFFsBBgjREGjkhffPD6gxAE2oyUfP.MuyAATaAHzKhFcPwEay5GjGQQcPS4+6A4QTTGzXsoaffGjf2PbPrMk+uGjf2PbPwQqp0Gjf2PbPrHd9AoFyBGTbjlg+AIUXAGjFtAfPyvquAEmXOGfPTbAuAY.fBHjiKgaPwoJLCHDB.RaPpBy.B0rzuFTbpBy.BQGXqFjA.JfP+W7oAEmXOGfPJtBoAoga.HD2iFZPwQZF9GjKb7YPrHd9AYKwcFTbzpZ8A4SabFzT9+dP9zFmAwltABdP9zFmAMVaaJ7IBQAcqFTb.p2JBQAcqFjRZ5hPBRcqAEGE5FiPvSCrAEQ+yHjtDSaPw4.P1HDgTkaPlO3MBoi8+FTb8dLNBA+kFGTuGiiP.pwyAEWuGiiPM.TzAQyq3HTIqJcPwwpk3HTOVPcPhaENBIE4TGTbYbANBYlrUGjIqdiP3pf0AEGM+biPJLl0AAFl1HjBiYcPrUqRdHjBiYcPwcpsdHDD.JdPXyXHBAGKnGTbHLFIBEM1sGzPPjhPQic6AEGDcshPQic6AE+AsHjBFydPwMsrtHDQytdPBG+KB0YYpGTbwBSLBU+EoGzGgHiPuT.5AEGiQLiPoIu4AEz7yHTZxadPwQmgzHTZxadPmIOMBwBZmGTbY4UMB4d2mGz1rViPSWK5Aw1ZugiP6v46AEmDcaiPPoz7AYI5zHjKHWePwsA8xHDCFgeP2MMLBULw4GTbSKqKB42P6Gji9whPAB99AEmRJohPE1G+A4ENnHTg8wePwYkGjHTg8wePBdIHBULw4GTbtBQGBU.C2GzW0ogP2ea7AEGDZegPnOF6Ac0VVHzJHRdPw8I2THjarxcPexMEBMsTRGTbexMEBUfQJGjQpXgPL5xvAEm62cgPTbAuAQw4YHjoVaaPwoiUbHDNVFaPlf8GBYRgtFTbRn0HBQAcqFzlBehPTP2pAMVarN+IBIfA4FTbU+7HBIfA4FzM4EhP7LauAEmlh7gP3AlvAY7ddHTp1qbPrwRQvHTp1qbPwwRQvHzjHcbPYV7KBEmADGTbFXzKB4Dw.GDD83hP.AkuAEmFzzhPxv8tAEbnqHjFwoaPwg1CpHj.FjaPrN+IBIfA4FzXs4C+hIjGWsePrUutdIjGWsePw42XcIjGWseP47IWBkPh5GTb0q8VBUut4Gjz3skPmaz8Awl6fpkPm3Z7AEWMhjkPVzE8AUirWIjLkYePwUiPVITSsgePrlKUBUoy4GTbiDyTB08K6GTh2EkPAB99AE2689jPkDI+AMinMITIQxePwUEIKITIQxePYhPRB0ZN6GTbcytQBYi34GDOoUjPFMy8AE2lkOjPWQH8AcaCCIDvGBePwIcMBITJKxdPRWiPBsNPmGTbRWiPBEoyhGDHgMjPnlm2AE2aLRjP.Sh1A8RQGIDumZcPw4d+IIDtpLcPnOnSBod2PGTbhmvTB4QjNGTN+lkPbJjyAwVN+lkPAjZxAEWN+lkPzMbvAcEEXIj622aPwYWZVIDZrnaPyqyTBgFK5FTbl3NTBgFK5FTmk8jPt7ytAEWEc2jPzGEuAY4tLIDao1aPwgglKID4.7aPBSpRBo5D.GTbs8ZRBAmIAGjAIhjPvYRvAEWrSdjPvYRvA0v3FIzRm.bPwklLFITIn7aP1YbQBwyr8FDaby.QBoKo2FTbSWsRBMSNqFjJqQkPyjypAEmWdekPyjypAUijZIDL70ZPw0fQcITK+9ZPnaxWBQYwyFTbBePXBs+x2FD.BHlP6RVuAEmO7KlP60uvA4C+hIT.olbPi0F0UBkPZh36AEGuJHkPZh36AQLQSIzM+6dPwsreTID004dP4YYUB4vXsGTbm3pUBgDTrGTMxdkPu2q5AE2P1hkPVtR5AkyuYIjBFbdPrkyuYIjmBmcPwE1lUIDvjncPoOsTBYaKaGTbww.TBwpMbGzob4jPUKc2AEm2rxjP9612AI08KIjhTFdPwYbPKIzE5NdPFGzRBUbPlGTbFGzRBIXOqGjk6xjPNLV6AEmY03jPZh36AQckPIjlH9dPi01SPlmPQmVhAw1SPlmPmfpxAwVtSsmPmfpxAE2CIwmPmfpxAImz7IDwdnbPwU8V8ITXUlbPXieeBkGHHGDaf98fB4f9uFTbJLCgBUXbtFDQPRnPP+XqAEmesSnPa3JqAoYgEJzFtxZPrIn1IJzFtxZPrcTuCJz3imbPw0cZCJDC.tbPKFwfBM30LGTb5jqfBsuKNGzKRInPg3xyAEmN4JnPHREzAs6ACJDDcGcPw0iUCJTlkMcP+R5fBMPJUGDaTKihB4wU6GDajxdgB4wU6GTbY4UgB4wU6GjM7SnPZKo9AEGEZRnPU5b9AM4RDJzh2eePrwck9IDvjncPwgp.9IjUggcP08VeBI+0WGTbBwMeBAoSWGz10tmPP5z0Aw1SPlmPP5z0Aw1SPlmPdb09AwVZX.mPdb09AwVZX.mPQmVhAMVaBMIiBsgqrFDa2SEjBsgqrFTbZ4MjBsgqrFDk6DoP.0ZqAE2yXFoPlwpqAcUrQJD7z.aPr8ofTJDX3ncPwEXuTJDkq1cPMINkBc38fGTbZbPkBo2PjGzboToPs834AEGaeUoP5MD4AILmUJzg2CdPwcg1UJDkq1cPH6gkBAFNZGDaqCVlB4ZCvFTbb4WlBYRgtFjqVmoP.XXqAEG.unoPZaHqAILoZJj1FxZPrY4tbJj1FxZPwEAPcJj1FxZPigYmBAfgsFTb0BemBYRgtFjIN3oPt1.rAwFdFEpPjxu1AEmPFFpPXhj2AcbtgJTdiEdPwwT6gJzV9QdPc4gnB0vnmGTb1BjnBowUjGzZpIpPmrP3AEGHTJpPz7a2Ak9zhJDX3ncProaukJD7z.aPwMj0kJjYr5ZP8MioBATqsFTb2BooBsgqrFjXKbpPa3JqAwl6fppPa3JqAwlPlQpPdb09AwF0UBpPdb09AEWz3+oPdb09A8AseJzh2eePrMFJbJj4.pbPwoC+aJTtGgbPsc8lBwKAFGTbfJ6lB8avCGDFZtoPRhXvAE2o7soPfU8vA4jVaJDWXXbPwY+MaJjVagbPMuvlBcBpJGDa.Y2kBs382GTbOFykB4wU6GjRsYoPdb09AwV6MKoPdb09AMVazwBsBsgqrFDazwBsB4wU6GDaAB2qB4wU6GDaAB2qBsgqrFzXsAf3zJDtAWYPwAf3zJjw0fYP2HJsBI0VZFTbtIFsB4MfbFz63OqPHzgmAEWbONqPvj6mAYR.yJjgtBZPwosbxJD2iFZPuCcrBw8ngFTbqOSrBw8ngFDEnBqPF5JnAEWOb.qPvj6mAMSsuJDBc3YPwghSuJj2.xYPSCwqBI0VZFTb9MsqBYbMXFjeS6pP3FbkAEmeS6pPHnyjAMMDuJT2.DYPwghSuJDrG6XPyT6qBY3JMFTb8vArB40iKFDEnBqPHnohAE25yDqPxRZhA8NzwJjrjlXPwosbxJjrjlXPlDvrBgflJFTbw83rB40iKFz63OqPFtRiAEmahQqPvdriAcinzJT2.DYPwAf3zJDB5LYP.HNsBgavUFzXs8IBGKzbCvaPw430FKjd8zaPUFpwB0Pu8FTbbtlwBAJO9FjLXXrPfxiuAEG3+VrPfxiuAozVEKDW30aPwQq8DKzEzxaP4HGwBIru6FTb919vBwVx5FjcDMrPnTftAEmKaJrPiCTtAEIsAKz3.kaPwElS.Kz3.kaPLA3uBEUn6FTb3HquB4a.9FDNx5qPTbcvAEGNx5qPC6EwAY6F+JDWXXbPwQSg+Jz8QebPjMCvB44GIGTbTFNvBYTaJGTX9FrP7X2xAEmKaJrPx7GyAsbfCKDN41bPwgFZDKDPy6bP0TTwBgYgPGTbBHhwBI+ERGjLParPuHH0AEmX9crPswt0AA94GKD.LocPw8UTHKDkq1cPeEExB8FahGTbeEExBAMFnGD4LerP6MN6AEWZHcrPm3Z7AMTRFKzJqTePw0gREKzKnhePAK8vBoJm5GTbkskvBURj7Gjn0ArPkDI+AEGkw8qPkDI+Aobd9JTrVuePwEff8JTObrePX2IuBUoy4GTbul6tB4Nf3Gj22qqPD1q8AEmC1nqPZnO8Assn4JjKnKePrknt5JDQytdPwIH75JTmkodPbnytBkOsoGTb0N3tBUEAoGDjzuqPUQP5AEmZkwqPUQP5AEfx7JzdCndPwcoK8JDnBrdPRLauBcPJrGTbMdiuB41SsGTop6qPS5j6AEWuc9qP41z6AMHr.KTtM8dPwgFhAKTtM8dP2KhvBQ5etGTbG1qvBAYrsGTGhLrPnOF6AE2rFNrPAYw5AAUsCKzobkdPw093CKTCiddPsO9vBM.ykGTbsO9vBMWBiGza5MrPIXT3AEW7PLrPdJ32AErXBKz8z3cPwEIsAKDTmycPPUMvBok2aGTbOX+uBQV0ZGj+L7qPcsY1AEG6i3qPVEF1AsJQ8JDW6ZcPwoVY7JzXUTcP6b6tBQaiRGTbKjvtBYfAPGDiepqPveEyAEmC1nqPamJxA4fM5JjOyMbPw4fM5Jjvd5aP4BqtBk.P5FTbisxtBAU30FTWYvqPs7orAEmUGzqPJz0qAIwZ9JDjn0ZPw4ry+JDEzsZPvCZvBQAcqFTbLj5vBQAcqFj6SUrPDLhqAE2y9arPxGMrAY1FHKDqvTaPi0VWTXsPTP2pAEGTvesPTP2pAUCfYKjfT2ZPwoADaKD7z.aPXFC2BoKwzFTbWLU2BQHU4Fj.02sP5X+uAEm6V5sPveowA4tkdKDfZ7bPw4tkdKTC.EcPppn2BUxpRGTbk4m2B0iETGTfd4sPRQN0AEGm93sPlIa0AMJBdKDtJXcPwopzcKjBiYcP.8W2BovXVGDapgUzBovXVGTbi4XzBAAfhGzd4KsPvwB5AE2jjQsPQic6AEytVKTzX2dPwcY3WKTzX2dPHbK1BofwrGTb4wX1BQzrqGD7qnsPcVl5AEGZKqsP0eQ5A84PaKzKEfdPwY8taKTZxadPvxB2Bkl7lGTbJYG2Bkl7lGzPrxsPrf14AEGOhysPt284A0WBcKzz0hdPrUrZdKzNb9dPwgYncKDTJMePacJ2B4Bx0GTbcz51BwfQ3GzxbpsPESb9AEWdLlsP9Mz9AckbXKTffuePwQCVWKTg8weP97j0BUXe7GTb6HD0BUXe7GTz9IsPESb9AEmY6BsPEvv8A8aaOKz82FePwgAHNKD5iwdP6BVyBsBhjGTbeEJyB4FqbGzWgxrPSKkzAE2WgxrPEXjxAMCRMKDitLbPwYv6MKDEWvaPYZxyBYp01FTbr3EzBgikwFjHeHsPlTnqAEGFfOsPTP2pA0EEVKDEzsZPi0l4rXsPBXPtAEm9ZPsPBXPtAs56RKDOy1aPwwEwQKDdfIbPxCWzBkp8JGDalVk1Bkp8JGTblVk1BMIRGGD2UnsPwY.wAE2DVmsPNQLvAgYTYKDPP4aPw0QyXKjLbuaPvO.1BoQb5FTbDqy0BIfA4Fj4rXsPBXPtAMVY";
const var pkwsTitlePath = Content.createPath();
pkwsTitlePath.loadFromData(kTitlePathData);
const var kTitleBounds = pkwsTitlePath.getBounds(1.0);
// offset and stroke that match the outline to the former rendered text at rest
const var kTitleNudge = [-0.19, -0.69];
const var kTitleStroke = 0.65;
const var kTitleHover = 0.01;      // title scale 1.00 -> 1.01 on header hover

Header_Pnl.setPaintRoutine(function(g)
{
	// title outline scaled about (24, 24)
	var s = 1.0 + kTitleHover * pkwsHdrAmt.v;
	var b = kTitleBounds;
	var r = [24 + kTitleNudge[0] + (b[0] - 24) * s, 24 + kTitleNudge[1] + (b[1] - 24) * s, b[2] * s, b[3] * s];
	g.setColour(C_WHITE);
	g.fillPath(pkwsTitlePath, r);
	g.drawPath(pkwsTitlePath, r, kTitleStroke * s);
	g.setColour(C_GREY);
	g.setFontWithSpacing(F_TEXT, 12, 0.03);
	g.drawAlignedText("Resonance Suppressor", [25, 35, 270, 16], "left");
});

// header row text, one font height tall, centred on local y 16
inline function pkwsHeaderText(g, text, x, w, size, align)
{
	g.drawAlignedText(text, [x, 16 - 0.5 * size, w, size], align);
}

GR_Pnl.setPaintRoutine(function(g)
{
	var w = this.getWidth();
	var h = this.getHeight();
	var pk = pkwsDisp[1];
	g.setColour(C_GREY);
	g.setFontWithSpacing(F_BOLD, 13, 0.03);
	pkwsHeaderText(g, "Peak Cut", 0, 70, 13, "left");
	g.setColour(C_OFF);
	g.setFont(F_MONO, 15);
	pkwsHeaderText(g, (pk > 0.05 ? "-" : "") + Engine.doubleToString(pk, 1) + " dB", 62, w - 62, 15, "right");
	g.setColour(C_DARK);
	g.fillRect([0, h - 2, w, 2]);
	g.setColour(C_ACCENT);
	g.fillRect([0, h - 2, w * Math.min(1.0, pk / kMeterMaxDb), 2]);
});

// ---- reduction screen -----------------------------------------------------------------------------
// HISE's CSS engine resolves "PKWSText" by name to the system fallback face; the screen labels use that same face.
const var F_SCREEN = "Helvetica";

inline function pkwsHzFull(f)
{
	return Math.round(f) + " Hz";
}
// dB scale labels at a fixed interface x
const var kDbLabelX = 814;

// selector handle centred on (cx, cy); e = eased hover / drag amount: +15 % size and a glow
inline function pkwsEdgeHandle(g, cx, cy, e, active)
{
	local s = 1.0 + 0.15 * e;
	local t = 1.2 * s;

	if (e > 0.001)
		g.drawDropShadow([cx - 8 * s, cy - 7 * s, 16 * s, 14 * s], Colours.withAlpha(C_OFF, 0.275 * e), 10);

	g.setColour(active ? C_WHITE : C_OFF);
	g.fillRect([cx - 2 * s, cy - 7 * s, 4 * s, 14 * s]);
	g.drawLine(cx - 5 * s, cx - 8 * s, cy - 3 * s, cy, t);
	g.drawLine(cx - 8 * s, cx - 5 * s, cy, cy + 3 * s, t);
	g.drawLine(cx + 5 * s, cx + 8 * s, cy - 3 * s, cy, t);
	g.drawLine(cx + 8 * s, cx + 5 * s, cy, cy + 3 * s, t);
}
Ovr_Pnl.data.drag = -1;
Ovr_Pnl.data.hover = -1;

// splitter hover / drag easing per edge (130 ms smoothstep); the timer runs only while an edge moves
const var kEdgeEaseMs = 130.0;
const var kEdgeTickMs = 15;
const var pkwsEdgeAmt = [0.0, 0.0];
const var PkwsEdgeTmr = Engine.createTimerObject();

inline function pkwsEdgeEase(i)
{
	local a = pkwsEdgeAmt[i];
	return a * a * (3.0 - 2.0 * a);
}

inline function pkwsEdgeTarget()
{
	return Ovr_Pnl.data.drag >= 0 ? Ovr_Pnl.data.drag : Ovr_Pnl.data.hover;
}

PkwsEdgeTmr.setTimerCallback(function()
{
	var on = pkwsEdgeTarget();
	var moving = false;
	var j = 0;

	for (j = 0; j < 2; j++)
	{
		var want = (on == j) ? 1.0 : 0.0;
		var a = pkwsEdgeAmt[j];
		var d = kEdgeTickMs / kEdgeEaseMs;
		a = want > a ? Math.min(want, a + d) : Math.max(want, a - d);
		pkwsEdgeAmt[j] = a;
		moving = moving || (a != want);
	}

	Ovr_Pnl.repaint();

	if (!moving)
		this.stopTimer();
});

inline function pkwsEdgeKick()
{
	if (!PkwsEdgeTmr.isTimerRunning())
		PkwsEdgeTmr.startTimer(kEdgeTickMs);
}

Ovr_Pnl.setPaintRoutine(function(g)
{
	var w = this.getWidth();
	var h = this.getHeight();
	var yT = 34.0;
	var yB = h - 26.0;
	var live = pkwsSt[2] > 0.5;
	var lo = Math.min(pkwsSt[0], pkwsSt[1]);
	var xL = pkwsX(lo, w);
	var xR = pkwsX(pkwsSt[1], w);
	var active = this.data.drag >= 0 ? this.data.drag : this.data.hover;
	var tint = live ? C_ACCENT : C_FAINT;
	var i = 0;
	var e = 0.0;
	var dw = 2.0;
	var x = 0.0;
	var y = 0.0;
	var px = 0.0;
	var py = 0.0;

	g.setColour(0x73000000);
	g.fillRect([0, 0, xL, h]);
	g.fillRect([xR, 0, w - xR, h]);

	g.setFont(F_SCREEN, 12);
	for (i = 0; i < kGridHz.length; i++)
	{
		x = Math.round(pkwsX(kGridHz[i], w));
		g.setColour(0x0FFFFFFF);
		g.fillRect([x, yT, 1, yB - yT]);
		g.setColour(C_FAINT);
		g.drawAlignedText(kGridLabel[i], [x - 20, h - 22, 40, 16], "centred");
	}

	for (i = 1; i <= 4; i++)
	{
		y = yT + i * (yB - yT) / 4.0;
		g.setColour(0x0FFFFFFF);
		g.fillRect([0, y, w, 1]);
		g.setColour(C_FAINT);
		g.drawAlignedText("-" + Math.round(i * kMeterMaxDb / 4), [kDbLabelX - this.get("x"), y - 17, 40, 15], "right");
	}
	g.setColour(C_DARK);
	g.fillRect([0, yT, w, 1]);

	// reduction curve
	pkwsFill.clear();
	pkwsLine.clear();
	px = pkwsX(pkwsBinHz(0), w);
	py = yT + pkwsDraw[0] * (yB - yT);
	pkwsFill.startNewSubPath(px, yT);
	pkwsFill.lineTo(px, py);
	pkwsLine.startNewSubPath(px, py);

	for (i = 1; i < kBins; i++)
	{
		x = pkwsX(pkwsBinHz(i), w);
		y = yT + pkwsDraw[i] * (yB - yT);
		pkwsFill.quadraticTo(px, py, (px + x) * 0.5, (py + y) * 0.5);
		pkwsLine.quadraticTo(px, py, (px + x) * 0.5, (py + y) * 0.5);
		px = x;
		py = y;
	}
	pkwsFill.lineTo(px, py);
	pkwsFill.lineTo(px, yT);
	pkwsFill.closeSubPath();
	pkwsLine.lineTo(px, py);

	g.setGradientFill([Colours.withAlpha(tint, 0.30), 0, yT, Colours.withAlpha(tint, 0.0), 0, yB, false]);
	g.fillPath(pkwsFill, -1);
	g.setColour(Colours.withAlpha(tint, 0.10));
	g.drawPath(pkwsLine, -1, 8.0);
	g.setColour(Colours.withAlpha(tint, 0.25));
	g.drawPath(pkwsLine, -1, 3.5);
	g.setColour(tint);
	g.drawPath(pkwsLine, -1, 1.4);

	// window edges
	for (i = 0; i < 2; i++)
	{
		x = Math.round(i == 0 ? xL : xR);
		e = pkwsEdgeEase(i);
		if (e > 0.001)
		{
			g.setColour(Colours.withAlpha(C_OFF, 0.035 * e));
			g.fillRect([x - 3, 8, 8, yB - 8]);
			g.setColour(Colours.withAlpha(C_OFF, 0.05 * e));
			g.fillRect([x - 1, 8, 4, yB - 8]);
		}
		// dashes 4 on / 4 off, 2 px -> 1.4 px while hovered / dragged
		g.setColour(Colours.withAlpha(C_OFF, active == i ? 1.0 : 0.6));
		dw = 2.0 * (1.0 - 0.30 * e);
		for (y = 8; y < yB; y += 8)
			g.fillRect([x + 1 - 0.5 * dw, y, dw, Math.min(4, yB - y)]);
		y = 0.5 * (8 + yB);
		pkwsEdgeHandle(g, x + 1, y, e, active == i);
	}

	if (xR - xL > 110.0)
	{
		g.drawAlignedText(pkwsHzFull(lo), [xL + 9, 7, 60, 15], "left");
		g.drawAlignedText(pkwsHzFull(pkwsSt[1]), [xR - 69, 7, 60, 15], "right");
	}
	else
	{
		g.drawAlignedText(pkwsHzFull(lo), [xL - 69, 7, 60, 15], "right");
		g.drawAlignedText(pkwsHzFull(pkwsSt[1]), [xR + 9, 7, 60, 15], "left");
	}

	if (pkwsSt[3] > 0.5)
	{
		g.setColour(C_ACCENT);
		g.setFontWithSpacing(F_BOLD, 12, 0.03);
		g.drawAlignedText("Listening To Removed Signal", [0, h - 54, w, 18], "centred");
	}
	if (!live)
	{
		g.setColour(C_GREY);
		g.setFontWithSpacing(F_BOLD, 16, 0.06);
		g.drawAlignedText("Bypassed", [0, yT, w, yB - yT], "centred");
	}
});

// dragging an edge moves its Range knob
Ovr_Pnl.setMouseCallback(function(event)
{
	var w = this.getWidth();
	var dl = Math.abs(event.x - pkwsX(Math.min(pkwsSt[0], pkwsSt[1]), w));
	var dr = Math.abs(event.x - pkwsX(pkwsSt[1], w));
	var near = Math.min(dl, dr) < 10.0 ? (dl <= dr ? 0 : 1) : -1;
	var hz = 0.0;

	if (event.clicked)
		this.data.drag = near;

	if (event.drag && this.data.drag >= 0)
	{
		hz = pkwsHzAt(event.x, w);
		if (this.data.drag == 0)
		{
			LowLim_Knb.setValue(Math.round(Math.range(hz, 60.0, pkwsSt[1] / kMinRatio)));
			LowLim_Knb.changed();
		}
		else
		{
			HighLim_Knb.setValue(Math.round(Math.range(hz, pkwsSt[0] * kMinRatio, 17000.0)));
			HighLim_Knb.changed();
		}
	}

	if (event.mouseUp)
		this.data.drag = -1;

	if (!event.hover)
		near = -1;

	if (near != this.data.hover || event.drag || event.mouseUp || event.clicked)
	{
		this.data.hover = near;
		this.repaint();
		pkwsEdgeKick();
	}
});

// ---- meter clock: time easing (rise 0.5, fall 0.2) + 5-tap smoothing across frequency ----
const var PkwsTmr = Engine.createTimerObject();
PkwsTmr.setTimerCallback(function()
{
	var live = pkwsSt[2] > 0.5;
	var moved = 0.0;
	var pk = 0.0;
	var v = 0.0;
	var i = 0;

	for (i = 0; i < kBins; i++)
	{
		v = live ? Math.range(SupPack.getValue(i), 0.0, 1.0) : 0.0;
		v = pkwsLive[i] + (v - pkwsLive[i]) * (v > pkwsLive[i] ? 0.5 : 0.2);
		moved = moved + Math.abs(v - pkwsLive[i]);
		pkwsLive[i] = v;
		pk = Math.max(pk, v);
	}

	for (i = 0; i < kBins; i++)
	{
		pkwsDraw[i] = (pkwsLive[Math.max(i - 2, 0)] + 4.0 * pkwsLive[Math.max(i - 1, 0)] + 6.0 * pkwsLive[i]
		            + 4.0 * pkwsLive[Math.min(i + 1, kBins - 1)] + pkwsLive[Math.min(i + 2, kBins - 1)]) / 16.0;
	}

	var key = pkwsSt[0] + 100000.0 * pkwsSt[1] + 10.0 * pkwsSt[2] + 100.0 * pkwsSt[3];
	if (moved > 0.0005 || key != pkwsDisp[0])
	{
		pkwsDisp[0] = key;
		Ovr_Pnl.repaint();
	}

	pk = Math.round(pk * kMeterMaxDb * 10.0) / 10.0;
	if (pk != pkwsDisp[1])
	{
		pkwsDisp[1] = pk;
		GR_Pnl.repaint();
	}
});
PkwsTmr.startTimer(40);

Content.setSuspendTimerCallback(function(isSuspended)
{
	if (isSuspended)
		PkwsTmr.stopTimer();
	else
		PkwsTmr.startTimer(40);
});

// ---- presets ----------------------------------------------------------------------------------------
inline function pkwsPresetLabel(path)
{
	local n = path.substring(path.lastIndexOf("/") + 1, path.length);
	local digits = "0123456789";
	if (n.length > 3 && n.charAt(2) == " " && digits.indexOf(n.charAt(0)) != -1 && digits.indexOf(n.charAt(1)) != -1)
		return n.substring(3, n.length);
	return n;
}

const var pkwsPresets = Engine.getUserPresetList();
const var pkwsPresetNames = [];
for (p in pkwsPresets)
	pkwsPresetNames.push(pkwsPresetLabel(p));

Preset_Pnl.set("popupMenuItems", pkwsPresetNames.join("\n"));

Preset_Pnl.setPaintRoutine(function(g)
{
	var name = pkwsPresetLabel(Engine.getCurrentUserPresetName());
	g.setColour(this.data.hover ? C_WHITE : C_OFF);
	g.setFont(F_TEXT, 16);
	g.drawAlignedText(name == "" ? "Default" : name, [10, 0, this.getWidth() - 30, this.getHeight()], "left");
	g.setColour(C_GREY);
	g.fillTriangle([this.getWidth() - 18, this.getHeight() * 0.5 - 2, 8, 5], Math.PI);
	g.setColour(C_DARK);
	g.fillRect([0, this.getHeight() - 1, this.getWidth(), 1]);
});

Preset_Pnl.setMouseCallback(function(event)
{
	if (event.result > 0)
		Engine.loadUserPreset(pkwsPresets[event.result - 1] + ".preset");

	if (event.hover != this.data.hover)
	{
		this.data.hover = event.hover;
		this.repaint();
	}
});

inline function pkwsArrow(panel, g, dir)
{
	g.setColour(panel.data.hover ? C_WHITE : C_GREY);
	g.fillTriangle([panel.getWidth() * 0.5 - 3, panel.getHeight() * 0.5 - 4, 6, 8], dir * Math.PI * 0.5);
}

inline function pkwsArrowMouse(panel, event, dir)
{
	if (event.clicked && pkwsPresets.length > 0)
	{
		if (dir > 0)
			Engine.loadNextUserPreset(false);
		else
			Engine.loadPreviousUserPreset(false);
	}

	if (event.hover != panel.data.hover)
	{
		panel.data.hover = event.hover;
		panel.repaint();
	}
}

PresetPrev_Pnl.setPaintRoutine(function(g) { pkwsArrow(this, g, -1); });
PresetNext_Pnl.setPaintRoutine(function(g) { pkwsArrow(this, g, 1); });
PresetPrev_Pnl.setMouseCallback(function(event) { pkwsArrowMouse(this, event, -1); });
PresetNext_Pnl.setMouseCallback(function(event) { pkwsArrowMouse(this, event, 1); });

// ---- info -----------------------------------------------------------------------------------------
// Info_Pnl opens InfoOverlay_Pnl (dimmed layer + card). Close: the x, a click outside the card, or the icon.
const var kInfoCard  = [220, 90, 440, 320];
const var kInfoClose = [220 + 440 - 34, 90 + 12, 22, 22];
const var kInfoUrl   = "https://www.beyhankilic.com";   // click target
const var kInfoLink  = "www.beyhankilic.com";           // shown text (centring, underline and hit area use it)
const var kInfoLinkY = 90 + 180;
const var kInfoSize  = 14;
const var kInfoBg     = 0xFF101010;      // card
const var kInfoRole   = 0xFFA0A0A0;      // credit roles (names: C_OFF)
const var kInfoNotice = 0xFF747474;      // licence notice
const var kInfoFooter = 0xFF5C5C5C;      // "Built with"

inline function pkwsInside(r, x, y) { return x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3]; }

// link hit area = its measured text box
inline function pkwsInfoLinkArea()
{
	local tw = Engine.getStringWidth(kInfoLink, F_TEXT, 13, 0.03);
	return [440 - 0.5 * tw - 4, kInfoLinkY - 2, tw + 8, 20];
}

// centred "<role>Beyhan KILIC" line: role regular / kInfoRole, name bold / C_OFF
inline function pkwsCreditLine(g, role, y)
{
	local rw = Engine.getStringWidth(role, F_TEXT, kInfoSize, 0.03);
	local nw = Engine.getStringWidth("Beyhan KILIC", F_BOLD, kInfoSize, 0.03);
	local x0 = 440 - 0.5 * (rw + nw);

	g.setColour(kInfoRole);
	g.setFontWithSpacing(F_TEXT, kInfoSize, 0.03);
	g.drawAlignedText(role, [x0, y, rw + 2, kInfoSize], "left");
	g.setColour(C_OFF);
	g.setFontWithSpacing(F_BOLD, kInfoSize, 0.03);
	g.drawAlignedText("Beyhan KILIC", [x0 + rw, y, nw + 2, kInfoSize], "left");
}

// ---- header / info icon interaction ----------------------------------------------------------------
// 220 ms smoothstep tweens (elapsed time; a reversal continues from the current value).
// Header hover: title scale (kTitleHover). Info icon: 1.04 hover, 1.02 pressed. Both open the info panel on release.
const var kTweenMs = 220.0;
const var pkwsHdrAmt  = { "v": 0.0, "from": 0.0, "to": 0.0, "t": 1.0, "t0": 0.0 };
const var pkwsInfoScl = { "v": 1.0, "from": 1.0, "to": 1.0, "t": 1.0, "t0": 0.0 };
const var PkwsTweenTmr = Engine.createTimerObject();

inline function pkwsTweenTo(tw, target)
{
	if (Math.abs(tw.to - target) < 0.0001)
		return;

	tw.from = tw.v;
	tw.to = target;
	tw.t = 0.0;
	tw.t0 = Engine.getUptime();

	if (!PkwsTweenTmr.isTimerRunning())
		PkwsTweenTmr.startTimer(15);
}

inline function pkwsTweenStep(tw)
{
	if (tw.t >= 1.0)
		return false;

	tw.t = Math.range((Engine.getUptime() - tw.t0) * 1000.0 / kTweenMs, 0.0, 1.0);   // elapsed time, not tick count
	tw.v = tw.from + (tw.to - tw.from) * tw.t * tw.t * (3.0 - 2.0 * tw.t);
	return true;
}

PkwsTweenTmr.setTimerCallback(function()
{
	var a = pkwsTweenStep(pkwsHdrAmt);
	var b = pkwsTweenStep(pkwsInfoScl);

	if (a)
		Header_Pnl.repaint();
	if (b)
		Info_Pnl.repaint();
	if (!a && !b)
		this.stopTimer();
});

// clickable header area = the measured title / subtitle text
const var kHdrArea = [20, 8, 25 + Math.max(Engine.getStringWidth("Peakwise", F_BOLD, 23 * (1 + kTitleHover), 0.04),
                                            Engine.getStringWidth("Resonance Suppressor", F_TEXT, 12, 0.03)) - 14, 46];

inline function pkwsInteractionReset()
{
	Header_Pnl.data.hover = false;
	Header_Pnl.data.down = false;
	Info_Pnl.data.hover = false;
	Info_Pnl.data.down = false;
	pkwsTweenTo(pkwsHdrAmt, 0.0);
	pkwsTweenTo(pkwsInfoScl, 1.0);
}

Header_Pnl.setMouseCallback(function(event)
{
	var inside = event.hover && pkwsInside(kHdrArea, event.x, event.y);

	if (event.clicked && inside)
		this.data.down = true;

	if (event.mouseUp)
	{
		var open = this.data.down && pkwsInside(kHdrArea, event.x, event.y);
		this.data.down = false;

		if (open)
		{
			pkwsShowInfo(true);
			return;
		}
	}

	this.data.hover = inside;
	pkwsTweenTo(pkwsHdrAmt, (inside || this.data.down) ? 1.0 : 0.0);
});

Info_Pnl.setPaintRoutine(function(g)
{
	var s = pkwsInfoScl.v;     // about the centre (14, 16)
	g.setColour(this.data.hover || InfoOverlay_Pnl.get("visible") ? C_WHITE : C_GREY);
	g.drawEllipse([14 - 8 * s, 16 - 8 * s, 16 * s, 16 * s], 1.4 * s);
	g.fillEllipse([14 - 1 * s, 16 - 4.8 * s, 2 * s, 2 * s]);
	g.fillRoundedRectangle([14 - 0.9 * s, 16 - 1.4 * s, 1.8 * s, 6 * s], 0.9 * s);
});

inline function pkwsShowInfo(show)
{
	InfoOverlay_Pnl.data.closeHover = false;
	InfoOverlay_Pnl.data.linkHover = false;
	pkwsInteractionReset();
	InfoOverlay_Pnl.set("visible", show);
	InfoOverlay_Pnl.repaint();
	Info_Pnl.repaint();
}

Info_Pnl.setMouseCallback(function(event)
{
	if (event.clicked)
		this.data.down = true;

	if (event.mouseUp)
	{
		var open = this.data.down && event.x >= 0 && event.y >= 0 && event.x < this.getWidth() && event.y < this.getHeight();
		this.data.down = false;

		if (open)
		{
			pkwsShowInfo(!InfoOverlay_Pnl.get("visible"));
			return;
		}
	}

	if (event.hover != this.data.hover)
	{
		this.data.hover = event.hover;
		this.repaint();
	}

	pkwsTweenTo(pkwsInfoScl, this.data.down ? 1.02 : (this.data.hover ? 1.04 : 1.0));
});

InfoOverlay_Pnl.setPaintRoutine(function(g)
{
	var c = kInfoCard;
	var x = c[0] + 30;
	var w = c[2] - 60;
	var k = kInfoClose;
	var la = pkwsInfoLinkArea();

	g.fillAll(0x26000000);
	g.setColour(kInfoBg);
	g.fillRect(c);
	g.setColour(C_DARK);
	g.drawRect(c, 1);

	g.setColour(this.data.closeHover ? C_WHITE : C_GREY);
	g.drawLine(k[0] + 6, k[0] + k[2] - 6, k[1] + 6, k[1] + k[3] - 6, 1.6);
	g.drawLine(k[0] + 6, k[0] + k[2] - 6, k[1] + k[3] - 6, k[1] + 6, 1.6);

	g.setColour(C_WHITE);
	g.setFontWithSpacing(F_BOLD, 23, 0.04);
	g.drawAlignedText(Engine.getName(), [x, c[1] + 30, w, 28], "centred");
	g.setColour(C_GREY);
	g.setFontWithSpacing(F_TEXT, 13, 0.03);
	g.drawAlignedText("Resonance Suppressor", [x, c[1] + 60, w, 18], "centred");
	g.setFont(F_MONO, 12);
	g.drawAlignedText("Version " + Engine.getVersion(), [x, c[1] + 80, w, 16], "centred");

	g.setColour(C_DARK);
	g.fillRect([c[0] + 60, c[1] + 110, c[2] - 120, 1]);

	pkwsCreditLine(g, "DSP & Programming: ", c[1] + 128);
	pkwsCreditLine(g, "UI/UX Design: ", c[1] + 150);

	g.setColour(this.data.linkHover ? C_WHITE : C_ACCENT);
	g.setFontWithSpacing(F_TEXT, 13, 0.03);
	g.drawAlignedText(kInfoLink, [x, kInfoLinkY, w, 16], "centred");
	if (this.data.linkHover)
		g.fillRect([la[0] + 4, kInfoLinkY + 15, la[2] - 8, 1]);

	g.setColour(C_DARK);
	g.fillRect([c[0] + 60, c[1] + 222, c[2] - 120, 1]);

	g.setColour(kInfoNotice);
	g.setFontWithSpacing(F_TEXT, 12, 0.03);
	g.drawAlignedText("Free software under the GNU General Public License,", [x, c[1] + 236, w, 16], "centred");
	g.drawAlignedText("version 3 or (at your option) any later version.", [x, c[1] + 253, w, 16], "centred");
	g.setColour(kInfoFooter);
	g.drawAlignedText("Built with JUCE & HISE", [x, c[1] + 282, w, 16], "centred");
});

InfoOverlay_Pnl.setMouseCallback(function(event)
{
	var onClose = pkwsInside(kInfoClose, event.x, event.y);
	var onLink = pkwsInside(pkwsInfoLinkArea(), event.x, event.y);

	if (event.clicked)
	{
		if (onLink)
			Engine.openWebsite(kInfoUrl);
		else if (onClose || !pkwsInside(kInfoCard, event.x, event.y))
		{
			pkwsShowInfo(false);
			return;
		}
	}

	if (onClose != this.data.closeHover || onLink != this.data.linkHover)
	{
		this.data.closeHover = onClose;
		this.data.linkHover = onLink;
		this.repaint();
	}
});

// ---- interface zoom -------------------------------------------------------------------------------
// 70 .. 200 % in 10 % steps: drag the grip or right-click it for the list (Resize_Pnl popup items). Dragging uses
// the pointer's screen distance from the window origin, (grip position + local) x zoom, so it does not feed back.
const var kZoomMin = 0.7;
const var kZoomMax = 2.0;

inline function pkwsZoomSnap(z)
{
	return Math.range(Math.round(z * 10.0) / 10.0, kZoomMin, kZoomMax);
}

const var pkwsZoomStart = Settings.getZoomLevel();

if (Math.abs(pkwsZoomSnap(pkwsZoomStart) - pkwsZoomStart) > 0.001)
	Settings.setZoomLevel(pkwsZoomSnap(pkwsZoomStart));

Resize_Pnl.setMouseCursor("BottomRightCornerResizeCursor", C_WHITE, [0, 0]);
Resize_Pnl.data.down = 0.0;

Resize_Pnl.setPaintRoutine(function(g)
{
	// corner triangle + a parallel 3.6 px line running past the window edges (no visible end caps)
	var p = Content.createPath();
	p.startNewSubPath(32, 19);
	p.lineTo(32, 32);
	p.lineTo(19, 32);
	p.closeSubPath();
	g.setColour(this.data.hover ? C_WHITE : C_FAINT);
	g.fillPath(p, -1);
	g.drawLine(63.16, -20, -20, 63.16, 3.6);
});

Resize_Pnl.setMouseCallback(function(event)
{
	if (event.result > 0)
	{
		Settings.setZoomLevel(pkwsZoomSnap(kZoomMin + (event.result - 1) * 0.1));
		return;
	}

	if (!event.rightClick)
	{
		var z = Settings.getZoomLevel();
		var d = ((this.get("x") + event.x) + (this.get("y") + event.y)) * z;

		if (event.clicked && (event.x < 6 || event.y < 6))      // ignore presses on the 6 px top / left margin
			this.data.down = 0.0;
		else if (event.clicked)
		{
			this.data.z0 = z;
			this.data.down = d;
			this.data.last = pkwsZoomSnap(z);
		}
		else if (event.drag && this.data.down > 0.0)
		{
			var raw = this.data.z0 * d / this.data.down;
			var q = pkwsZoomSnap(raw);

			// dead band around each step
			if (Math.abs(q - this.data.last) > 0.001 && (Math.abs(raw - q) < 0.04 || raw <= kZoomMin || raw >= kZoomMax))
			{
				this.data.last = q;
				Settings.setZoomLevel(q);
			}
		}
		else if (event.mouseUp)
			this.data.down = 0.0;
	}

	if (event.hover != this.data.hover)
	{
		this.data.hover = event.hover;
		this.repaint();
	}
});

const var pkwsPresetHandler = Engine.createUserPresetHandler();
pkwsPresetHandler.setPostCallback(function(presetFile) { Preset_Pnl.repaint(); });

PkwsSync.addListener("view", "repaint state views", function(component, value)
{
	Ovr_Pnl.repaint();
	GR_Pnl.repaint();
});
