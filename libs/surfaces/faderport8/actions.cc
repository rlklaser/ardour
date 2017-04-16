/* Faderport 8 Control Surface
 * This is the button "Controller" of the MVC surface inteface,
 * see callbacks.cc for the "View".
 *
 * Copyright (C) 2017 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "ardour/dB.h"
#include "ardour/session.h"
#include "ardour/session_configuration.h"
#include "ardour/types.h"

#include "gtkmm2ext/actions.h"

#include "faderport8.h"

#include "pbd/i18n.h"

using namespace ARDOUR;
using namespace ArdourSurface;
using namespace std;
using namespace ArdourSurface::FP8Types;

#define BindMethod(ID, CB) \
	_ctrls.button (FP8Controls::ID).released.connect_same_thread (button_connections, boost::bind (&FaderPort8:: CB, this));

#define BindFunction(ID, ACT, CB, ...) \
	_ctrls.button (FP8Controls::ID). ACT .connect_same_thread (button_connections, boost::bind (&FaderPort8:: CB, this, __VA_ARGS__));

#define BindAction(ID, GRP, ITEM) \
	_ctrls.button (FP8Controls::ID).released.connect_same_thread (button_connections, boost::bind (&FaderPort8::button_action, this, GRP, ITEM));

#define BindUserAction(ID) \
	_ctrls.button (ID).pressed.connect_same_thread (button_connections, boost::bind (&FaderPort8::button_user, this, true, ID)); \
_ctrls.button (ID).released.connect_same_thread (button_connections, boost::bind (&FaderPort8::button_user, this, false, ID));

void
FaderPort8::setup_actions ()
{
	BindMethod (BtnPlay, button_play);
	BindMethod (BtnStop, button_stop);
	BindMethod (BtnLoop, button_loop);
	BindMethod (BtnRecord, button_record);
	BindMethod (BtnClick, button_metronom);
	BindAction (BtnRedo, "Editor", "redo");

	BindAction (BtnSave, "Common", "Save");
	BindAction (BtnUndo, "Editor", "undo");
	BindAction (BtnRedo, "Editor", "redo");

	BindAction (BtnSoloClear, "Main", "cancel-solo");
	BindMethod (BtnMuteClear, button_mute_clear);

	BindMethod (FP8Controls::BtnArmAll, button_arm_all);

	BindFunction (BtnRewind, pressed, button_varispeed, false);
	BindFunction (BtnFastForward, pressed, button_varispeed, true);

	BindFunction (BtnPrev, released, button_prev_next, false);
	BindFunction (BtnNext, released, button_prev_next, true);

	BindFunction (BtnArm, pressed, button_arm, true);
	BindFunction (BtnArm, released, button_arm, false);

	BindFunction (BtnAOff, released, button_automation, ARDOUR::Off);
	BindFunction (BtnATouch, released, button_automation, ARDOUR::Touch);
	BindFunction (BtnARead, released, button_automation, ARDOUR::Play);
	BindFunction (BtnAWrite, released, button_automation, ARDOUR::Write);

	_ctrls.button (FP8Controls::BtnEncoder).pressed.connect_same_thread (button_connections, boost::bind (&FaderPort8::button_encoder, this));
	_ctrls.button (FP8Controls::BtnParam).pressed.connect_same_thread (button_connections, boost::bind (&FaderPort8::button_parameter, this));


	BindAction (BtnBypass, "Mixer", "ab-plugins");
	BindAction (BtnBypassAll, "Mixer", "ab-plugins"); // XXX

	BindAction (BtnMacro, "Mixer", "show-editor");
	BindAction (BtnLink, "Window", "show-mixer");

	BindAction (BtnOpen, "Common", "addExistingAudioFiles");
	BindAction (BtnLock, "Editor", "lock");

	// user-specific
	for (FP8Controls::UserButtonMap::const_iterator i = _ctrls.user_buttons ().begin ();
			i != _ctrls.user_buttons ().end (); ++i) {
		BindUserAction ((*i).first);
	}
}

void
FaderPort8::button_play ()
{
	if (session->transport_rolling ()) {
		if (session->transport_speed () != 1.0) {
			session->request_transport_speed (1.0);
		} else {
			transport_stop ();
		}
	} else {
		transport_play ();
	}
}

void
FaderPort8::button_stop ()
{
	if (session->transport_rolling ()) {
		transport_stop ();
	} else {
		AccessAction ("Transport", "GotoStart");
	}
}

void
FaderPort8::button_record ()
{
	set_record_enable (!get_record_enabled ());
}

void
FaderPort8::button_loop ()
{
	loop_toggle ();
}

void
FaderPort8::button_metronom ()
{
	Config->set_clicking (!Config->get_clicking ());
}

void
FaderPort8::button_automation (ARDOUR::AutoState as)
{
	FaderMode fadermode = _ctrls.fader_mode ();
	switch (fadermode) {
		case ModePlugins:
#if 0 // Plugin Control Automation Mode
			for ( std::list <ProcessorCtrl>::iterator i = _proc_params.begin(); i != _proc_params.end(); ++i) {
				((*i).ac)->set_automation_state (as);
			}
#endif
			return;
		case ModeSend:
			if (first_selected_stripable()) {
#if 0 // Send Level Automation
				boost::shared_ptr<Stripable> s = first_selected_stripable();
				boost::shared_ptr<AutomationControl> send;
				uint32_t i = 0;
				while (0 != (send = s->send_level_controllable (i))) {
					send->set_automation_state (as);
					++i;
				}
#endif
			}
			return;
		default:
			break;
	}

	// apply to all selected tracks
	StripableList all;
	session->get_stripables (all);
	for (StripableList::const_iterator i = all.begin(); i != all.end(); ++i) {
		if ((*i)->is_master() || (*i)->is_monitor()) {
			continue;
		}
		if (!(*i)->is_selected()) {
			continue;
		}
		boost::shared_ptr<AutomationControl> ac;
		switch (fadermode) {
			case ModeTrack:
				ac = (*i)->gain_control ();
				break;
			case ModePan:
				ac = (*i)->pan_azimuth_control ();
				break;
			default:
				break;
		}
		if (ac) {
			ac->set_automation_state (as);
		}
	}
}

void
FaderPort8::button_varispeed (bool ffw)
{
	/* pressing both rew + ffwd -> return to zero */
	FP8ButtonInterface& b_rew = _ctrls.button (FP8Controls::BtnRewind);
	FP8ButtonInterface& b_ffw = _ctrls.button (FP8Controls::BtnFastForward);
	if (b_rew.is_pressed () && b_ffw.is_pressed ()){
		// stop key-repeat
		dynamic_cast<FP8RepeatButton*>(&b_ffw)->stop_repeat();
		dynamic_cast<FP8RepeatButton*>(&b_rew)->stop_repeat();
		session->request_locate (0, false);
		return;
	}

	// switch play direction, if needed
	if (ffw) {
		if (session->transport_speed () <= 0) {
			session->request_transport_speed (1.0);
			return ;
		}
	} else {
		if (session->transport_speed () >= 0) {
			session->request_transport_speed (-1.0);
			return ;
		}
	}
	// incremetally increase speed. double speed every 10 clicks
	// (keypress auto-repeat is 100ms)
	float maxspeed = Config->get_shuttle_max_speed();
	float speed = exp2f(0.1f) * session->transport_speed ();
	speed = std::max (-maxspeed, std::min (maxspeed, speed));
	session->request_transport_speed (speed, false);
}

void
FaderPort8::button_mute_clear ()
{
	StripableList all;
	session->get_stripables (all);
	boost::shared_ptr<ControlList> cl (new ControlList);
	for (StripableList::const_iterator i = all.begin(); i != all.end(); ++i) {
		if ((*i)->is_master() || (*i)->is_monitor()) {
			continue;
		}
		boost::shared_ptr<AutomationControl> ac = (*i)->mute_control();
		if (ac && ac->get_value () > 0) {
			if (ac->automation_state() == Touch && !ac->touching ()) {
				ac->start_touch (ac->session().transport_frame());
			}
			cl->push_back (ac);
		}
	}
	session->set_controls (cl, 0.0, PBD::Controllable::UseGroup);
}

void
FaderPort8::button_arm (bool press)
{
	FaderMode fadermode = _ctrls.fader_mode ();
	if (fadermode == ModeTrack || fadermode == ModePan) {
		_ctrls.button (FP8Controls::BtnArm).set_active (press);
		ARMButtonChange (press);
	}
}

void
FaderPort8::button_arm_all ()
{
	BasicUI::all_tracks_rec_in ();
}

void
FaderPort8::button_action (const std::string& group, const std::string& item)
{
	AccessAction (group, item);
}

void
FaderPort8::button_prev_next (bool next)
{
	switch (_ctrls.nav_mode()) {
		case NavChannel:
			select_prev_next (next);
			break;
		case NavMaster:
		case NavScroll:
			bank (!next, false);
			break;
		case NavBank:
			bank (!next, true);
			break;
		case NavZoom:
			if (next) {
				VerticalZoomInSelected ();
			} else {
				VerticalZoomOutSelected ();
			}
			break;
		case NavSection:
			if (next) {
				AccessAction ("Region", "nudge-forward");
			} else {
				AccessAction ("Region", "nudge-backward");
			}
			break;
		case NavMarker:
			if (next) {
				next_marker ();
			} else {
				prev_marker ();
			}
			break;
	}
}

/* handle navigation encoder press */
void
FaderPort8::button_encoder ()
{
	/* special-case metronome level */
	if (_ctrls.button (FP8Controls::BtnClick).is_pressed ()) {
		Config->set_click_gain (1.0);
		_ctrls.button (FP8Controls::BtnClick).ignore_release();
		return;
	}
	switch (_ctrls.nav_mode()) {
		case NavZoom:
			ZoomToSession (); // XXX undo zoom
			break;
		case NavScroll:
			ZoomToSession ();
			break;
		case NavChannel:
		case NavBank:
			move_selected_into_view ();
			break;
		case NavMaster:
			{
				/* master || monitor level -- reset to 0dB */
				boost::shared_ptr<AutomationControl> ac;
				if (session->monitor_active() && !_ctrls.button (FP8Controls::BtnMaster).is_pressed ()) {
					ac = session->monitor_out()->gain_control ();
				} else if (session->master_out()) {
					ac = session->master_out()->gain_control ();
				}
				if (ac) {
					if (ac->automation_state() == Touch && !ac->touching ()) {
						ac->start_touch (ac->session().transport_frame());
					}
					ac->set_value (ac->normal(), PBD::Controllable::NoGroup);
				}
			}
			break;
		case NavSection:
			// TODO nudge
			break;
		case NavMarker:
			{
				string markername;
				/* Don't add another mark if one exists within 1/100th of a second of
				 * the current position and we're not rolling.
				 */
				framepos_t where = session->audible_frame();
				if (session->transport_stopped() && session->locations()->mark_at (where, session->frame_rate() / 100.0)) {
					return;
				}

				session->locations()->next_available_name (markername,"mark");
				add_marker (markername);
			}
			break;
	}
}

/* handle navigation encoder turn */
void
FaderPort8::encoder_navigate (bool neg, int steps)
{
	/* special-case metronome level */
	if (_ctrls.button (FP8Controls::BtnClick).is_pressed ()) {
		// compare to ARDOUR_UI::click_button_scroll()
		gain_t gain = Config->get_click_gain();
		float gain_db = accurate_coefficient_to_dB (gain);
		gain_db += (neg ? -1.f : 1.f) * steps;
		gain_db = std::max (-60.f, gain_db);
		gain = dB_to_coefficient (gain_db);
		gain = std::min (gain, Config->get_max_gain());
		Config->set_click_gain (gain);
		_ctrls.button (FP8Controls::BtnClick).ignore_release();
		return;
	}

	switch (_ctrls.nav_mode()) {
		case NavChannel:
			if (neg) {
				StepTracksUp ();
			} else {
				StepTracksDown ();
			}
			break;
		case NavZoom:
			if (neg) {
				ZoomOut ();
			} else {
				ZoomIn ();
			}
			break;
		case NavMarker:
		case NavScroll:
			ScrollTimeline ((neg ? -1.f : 1.f) * steps / (shift_mod() ? 1024.f : 256.f));
			break;
		case NavBank:
			bank (neg, false);
			break;
		case NavMaster:
			{
				/* master || monitor level */
				boost::shared_ptr<AutomationControl> ac;
				if (session->monitor_active() && !_ctrls.button (FP8Controls::BtnMaster).is_pressed ()) {
					ac = session->monitor_out()->gain_control ();
				} else if (session->master_out()) {
					ac = session->master_out()->gain_control ();
				}
				if (ac) {
					double v = ac->internal_to_interface (ac->get_value());
					v = std::max (0.0, std::min (1.0, v + steps * (neg ? -.01 : .01)));
					if (ac->automation_state() == Touch && !ac->touching ()) {
						ac->start_touch (ac->session().transport_frame());
					}
					ac->set_value (ac->interface_to_internal(v), PBD::Controllable::NoGroup);
				}
			}
			break;
		case NavSection:
			if (neg) {
				AccessAction ("Common", "nudge-playhead-backward");
			} else {
				AccessAction ("Common", "nudge-playhead-forward");
			}
			break;
	}
}

/* handle pan/param encoder press */
void
FaderPort8::button_parameter ()
{
	switch (_ctrls.fader_mode()) {
		case ModeTrack:
		case ModePan:
			{
				boost::shared_ptr<Stripable> s = first_selected_stripable();
				if (s) {
					boost::shared_ptr<AutomationControl> ac;
					if (shift_mod () || _ctrls.fader_mode() == ModePan) {
						ac = s->pan_width_control ();
					} else {
						ac = s->pan_azimuth_control ();
					}
					if (ac) {
						if (ac->automation_state() == Touch && !ac->touching ()) {
							ac->start_touch (ac->session().transport_frame());
						}
						ac->set_value (ac->normal(), PBD::Controllable::UseGroup);
					}
				}
			}
			break;
		case ModePlugins:
			break;
		case ModeSend:
			break;
	}
}

/* handle pan/param encoder turn */
void
FaderPort8::encoder_parameter (bool neg, int steps)
{
	switch (_ctrls.fader_mode()) {
		case ModeTrack:
		case ModePan:
			{
				boost::shared_ptr<Stripable> s = first_selected_stripable();
				if (s) {
					boost::shared_ptr<AutomationControl> ac;
					if (shift_mod () || _ctrls.fader_mode() == ModePan) {
						ac = s->pan_width_control ();
					} else {
						ac = s->pan_azimuth_control ();
					}
					if (ac) {
						double v = ac->internal_to_interface (ac->get_value());
						v = std::max (0.0, std::min (1.0, v + steps * (neg ? -.01 : .01)));
						if (ac->automation_state() == Touch && !ac->touching ()) {
							ac->start_touch (ac->session().transport_frame());
						}
						ac->set_value (ac->interface_to_internal(v), PBD::Controllable::UseGroup);
					}
				}
			}
			break;
		case ModePlugins:
		case ModeSend:
			while (steps > 0) {
				bank_param (neg, false);
				--steps;
			}
			break;
	}
}

/* handle user-specific actions */
void
FaderPort8::button_user (bool press, FP8Controls::ButtonId btn)
{
	_user_action_map[btn].call (*this, press);
}