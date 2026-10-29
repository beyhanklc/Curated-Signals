/*  Peakwise - palette, fonts, CSS knob, button look and feel
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

Engine.loadFontAs("{PROJECT_FOLDER}Fonts/Lato-Regular.ttf", "PKWSText");
Engine.loadFontAs("{PROJECT_FOLDER}Fonts/Lato-Bold.ttf", "PKWSBold");
Engine.loadFontAs("{PROJECT_FOLDER}Fonts/NotoSansMono-Medium.ttf", "PKWSMono");

const var C_BLACK  = 0xFF000000;
const var C_SCREEN = 0xFF111111;
const var C_DARK   = 0xFF303030;
const var C_FAINT  = 0xFF555555;
const var C_GREY   = 0xFF8C8C8C;
const var C_OFF    = 0xFFECE9E2;
const var C_WHITE  = 0xFFFFFFFF;
const var C_ACCENT = 0xFF24A8E0;      // accent blue

const var F_TEXT = "PKWSText";
const var F_BOLD = "PKWSBold";
const var F_MONO = "PKWSMono";

// ---- CSS knob -------------------------------------------------------------------------------------
// Reference 90 x 114: a 24 px title strip over a 90 x 90 knob square. The height sets the size, the width
// only centres it (pkwsKnobLayout). ::before draws the title and track, ::after the value arc; hover grows
// the body, shrinks both arcs to 0.95833 and the title to 0.98.
// HISE CSS constraints: interrupted transitions resume by property NAME, so element / ::before / ::after
// animate different properties; pseudo-elements need per-property transitions ("transition: all" breaks
// "position: absolute"); use the "border" shorthand, not "border-width". Arc paths carry (0,0) / (90,90)
// anchors because HISE fits a background path to its area.

const var KNOB_BOX = 90;
const var KNOB_TITLE_H = 24;
const var KNOB_H0 = KNOB_BOX + KNOB_TITLE_H;   // reference height
const var KNOB_SCALE_MIN = 0.15;               // smallest text / geometry scale
const var KNOB_R   = 36.0;
const var KNOB_A0  = -2.356194;
const var KNOB_A1  =  2.356194;
const var KNOB_STROKE_PX = 2.5;
const var KNOB_BODY_RATIO = 28.5 / 34.75;      // body radius / arc inner edge
const var KNOB_BODY_HOVER = 30.1 / 28.5;
const var KNOB_TITLE_PX = 14.4;
const var KNOB_VALUE_PX = 11.5;
const var KNOB_TITLE_HOVER = 0.98;

const var LafKnob = Content.createLocalLookAndFeel();
LafKnob.setInlineStyleSheet("
.scriptslider
{
	margin-top: var(--bodyTop);
	margin-left: var(--bodyX);
	margin-right: var(--bodyX);
	margin-bottom: var(--bodyBottom);
	background-color: #1a1a1a;
	border-radius: var(--bodyR);
	color: #ece9e2;
	font-family: PKWSText;
	font-size: var(--valueSize);
	padding-left: -60px;
	padding-right: -60px;
	text-align: center;
	vertical-align: middle;
	transition: margin 110ms ease-in-out;
	transition: border 110ms ease-in-out;
}

.scriptslider:hover
{
	margin-top: var(--bodyTopHover);
	margin-left: var(--bodyXHover);
	margin-right: var(--bodyXHover);
	margin-bottom: var(--bodyBottomHover);
	border-radius: var(--bodyRHover);
	color: #ffffff;
}

.scriptslider::before
{
	position: absolute;
	content: var(--title);
	background-image: var(--track);
	background-color: #303030;
	box-sizing: border-box;
	border: 0px solid transparent;
	margin-top: var(--squareTop);
	margin-left: var(--squareX);
	margin-right: var(--squareX);
	margin-bottom: var(--squareBottom);
	padding-top: var(--titlePadTop);
	padding-bottom: var(--titlePadBottom);
	padding-left: var(--titlePadX);
	padding-right: var(--titlePadX);
	color: #8c8c8c;
	font-family: PKWSBold;
	font-size: var(--titleSize);
	letter-spacing: 0.03em;
	text-align: center;
	vertical-align: middle;
	transition: border 110ms ease-in-out;
	transition: color 110ms ease-in-out;
	transition: font-size 110ms ease-in-out;
}

.scriptslider::after
{
	position: absolute;
	content: '';
	background-image: var(--arc);
	background-color: #ece9e2;
	box-shadow: 0px 0px 7px rgba(236, 233, 226, 0.75);
	margin-top: var(--squareTop);
	margin-left: var(--squareX);
	margin-right: var(--squareX);
	margin-bottom: var(--squareBottom);
	transform: scale(1.0);
	transition: transform 110ms ease-in-out;
}

.scriptslider:hover::before
{
	border: 2.08333% solid transparent;
	color: #ffffff;
	font-size: var(--titleSizeHover);
}

.scriptslider:hover::after
{
	transform: scale(0.95833);
}
");

// knob travel 0..1, including a middlePosition skew
inline function pkwsKnobPos(knob)
{
	local lo = knob.get("min");
	local hi = knob.get("max");
	local mid = knob.get("middlePosition");
	local u = Math.range((knob.getValue() - lo) / (hi - lo), 0.0, 1.0);

	if (mid > lo && mid < hi)
		u = Math.pow(u, Math.log(0.5) / Math.log((mid - lo) / (hi - lo)));

	return u;
}

// value arc for travel u in the 90 x 90 path box
inline function pkwsArcPath(u, stroke)
{
	local c = KNOB_BOX * 0.5;
	local out = Content.createPath();
	local arc = Content.createPath();

	if (u > 0.002)
	{
		arc.addArc([c - KNOB_R, c - KNOB_R, 2.0 * KNOB_R, 2.0 * KNOB_R], KNOB_A0, KNOB_A0 + (KNOB_A1 - KNOB_A0) * u);
		out = arc.createStrokedPath({ "Thickness": stroke, "EndCapStyle": "rounded", "JointStyle": "curved" }, []);
	}

	out.startNewSubPath(0, 0);
	out.startNewSubPath(KNOB_BOX, KNOB_BOX);
	return out;
}

const var pkwsKnobStroke = {};                  // knob id -> current stroke in path units

inline function pkwsPx(v)
{
	return Engine.doubleToString(v, 3) + "px";
}

// CSS variables from the knob's current width / height
inline function pkwsKnobLayout(knob)
{
	local w = knob.get("width");
	local h = knob.get("height");
	local f = Math.range(h / KNOB_H0, KNOB_SCALE_MIN, 1.0);
	local title = KNOB_TITLE_H * f;
	local side = Math.max(h - title, KNOB_BOX * KNOB_SCALE_MIN);
	local g = side / KNOB_BOX;
	local stroke = KNOB_STROKE_PX * Math.max(1.0, g);
	local body = KNOB_BODY_RATIO * (KNOB_R * g - 0.5 * stroke);
	local bodyHover = body * KNOB_BODY_HOVER;
	local cy = title + 0.5 * side;

	knob.setStyleSheetProperty("bodyTop", pkwsPx(cy - body), "");
	knob.setStyleSheetProperty("bodyX", pkwsPx(0.5 * w - body), "");
	knob.setStyleSheetProperty("bodyBottom", pkwsPx(h - cy - body), "");
	knob.setStyleSheetProperty("bodyR", pkwsPx(body), "");
	knob.setStyleSheetProperty("bodyTopHover", pkwsPx(cy - bodyHover), "");
	knob.setStyleSheetProperty("bodyXHover", pkwsPx(0.5 * w - bodyHover), "");
	knob.setStyleSheetProperty("bodyBottomHover", pkwsPx(h - cy - bodyHover), "");
	knob.setStyleSheetProperty("bodyRHover", pkwsPx(bodyHover), "");
	knob.setStyleSheetProperty("valueSize", pkwsPx(KNOB_VALUE_PX * f), "");

	knob.setStyleSheetProperty("squareTop", pkwsPx(title), "");
	knob.setStyleSheetProperty("squareX", pkwsPx(0.5 * (w - side)), "");
	knob.setStyleSheetProperty("squareBottom", pkwsPx(h - title - side), "");

	// title box: 300 px wide centred on width / 2, rows 6 f .. 26 f
	knob.setStyleSheetProperty("titlePadTop", pkwsPx(6.0 * f - title), "");
	knob.setStyleSheetProperty("titlePadBottom", pkwsPx(side - (26.0 * f - title)), "");
	knob.setStyleSheetProperty("titlePadX", pkwsPx(0.5 * side - 150.0), "");
	knob.setStyleSheetProperty("titleSize", pkwsPx(KNOB_TITLE_PX * f), "");
	knob.setStyleSheetProperty("titleSizeHover", pkwsPx(KNOB_TITLE_PX * f * KNOB_TITLE_HOVER), "");

	pkwsKnobStroke[knob.getId()] = stroke / g;
	knob.setStyleSheetProperty("track", pkwsArcPath(1.0, stroke / g), "path");
	knob.setStyleSheetProperty("arc", pkwsArcPath(pkwsKnobPos(knob), stroke / g), "path");
}

inline function pkwsKnobUpdate(knob)
{
	knob.setStyleSheetProperty("arc", pkwsArcPath(pkwsKnobPos(knob), pkwsKnobStroke[knob.getId()]), "path");
}

// ---- buttons --------------------------------------------------------------------------------------
// Power: status dot + "Active" / "Bypassed". padding-top 2px aligns the caption with the dot (JUCE snaps
// centred text to whole pixels). Hover: caption 13 -> 13.52 px, dot 0.95 (1.02 pressed), anchored at its
// right edge.
const var LafPower = Content.createLocalLookAndFeel();
LafPower.setInlineStyleSheet("
.scriptbutton
{
	background-color: transparent;
	color: #8c8c8c;
	content: 'Bypassed';
	font-family: PKWSBold;
	font-size: 13px;
	letter-spacing: 0.03em;
	text-align: left;
	vertical-align: middle;
	padding-left: 18px;
	padding-top: 2px;
	transition: font-size 220ms ease-in-out;
}

.scriptbutton:hover
{
	color: #ece9e2;
	font-size: 13.52px;
}

.scriptbutton:checked
{
	color: #24a8e0;
	content: 'Active';
}

.scriptbutton::before
{
	position: absolute;
	content: '';
	background-color: #8c8c8c;
	width: 10px;
	margin-left: 4px;
	margin-top: calc(50% - 3px);
	margin-bottom: calc(50% - 3px);
	border-radius: 3px;
	transform: translate(0px, 0px) scale(1.0);
	transition: transform 220ms ease-in-out;
}

.scriptbutton:hover::before
{
	background-color: #ece9e2;
	transform: translate(0.15789px, 0px) scale(0.95);
}

.scriptbutton:active::before
{
	transform: translate(-0.05882px, 0px) scale(1.02);
}

.scriptbutton:checked::before
{
	background-color: #24a8e0;
}
");

// Listen: headphone icon + caption (padding-top 6px: caption 3 px below centre). Hover: caption 13 -> 13.52 px,
// icon 0.95 (1.02 pressed), anchored at its right edge (pkwsListenLayout).
const var LISTEN_ICON_HOVER = 0.95;
const var LISTEN_ICON_PRESS = 1.02;

const var LafListen = Content.createLocalLookAndFeel();
LafListen.setInlineStyleSheet("
.scriptbutton
{
	background-color: transparent;
	color: #8c8c8c;
	content: 'Listen';
	font-family: PKWSBold;
	font-size: 13px;
	letter-spacing: 0.03em;
	text-align: left;
	vertical-align: middle;
	padding-left: 28px;
	padding-top: 6px;
	transition: font-size 220ms ease-in-out;
}

.scriptbutton:hover
{
	color: #ece9e2;
	font-size: 13.52px;
}

.scriptbutton:checked
{
	color: #24a8e0;
}

.scriptbutton::before
{
	position: absolute;
	content: '';
	background-image: var(--icon);
	background-color: #8c8c8c;
	width: 18px;
	margin-top: calc(50% - 9px);
	margin-bottom: calc(50% - 9px);
	transform: translate(0px, 0px) scale(1.0);
	transition: transform 220ms ease-in-out;
}

.scriptbutton:hover::before
{
	background-color: #ece9e2;
	transform: var(--iconHover);
}

.scriptbutton:active::before
{
	transform: var(--iconPress);
}

.scriptbutton:checked::before
{
	background-color: #24a8e0;
}
");

// headphone icon as one filled path in an 18 x 18 box
inline function pkwsListenIcon()
{
	local arc = Content.createPath();
	arc.addArc([2, 2, 14, 14], -1.75, 1.75);
	local icon = arc.createStrokedPath({ "Thickness": 1.6, "EndCapStyle": "butt", "JointStyle": "mitered" }, []);
	icon.addRoundedRectangle([1, 10, 4, 7], 1.5);
	icon.addRoundedRectangle([13, 10, 4, 7], 1.5);
	icon.startNewSubPath(0, 0);
	icon.startNewSubPath(18, 18);
	return icon;
}

// scale s about the box centre, translated so the icon's right edge (x 17) stays put
inline function pkwsListenIconTransform(s)
{
	return "translate(" + pkwsPx(8.0 * (1.0 - s) / s) + ", 0px) scale(" + s + ")";
}

inline function pkwsListenLayout(button)
{
	button.setStyleSheetProperty("iconHover", pkwsListenIconTransform(LISTEN_ICON_HOVER), "");
	button.setStyleSheetProperty("iconPress", pkwsListenIconTransform(LISTEN_ICON_PRESS), "");
	button.setStyleSheetProperty("icon", pkwsListenIcon(), "path");
}
