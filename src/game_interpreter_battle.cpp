/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * EasyRPG Player is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with EasyRPG Player. If not, see <http://www.gnu.org/licenses/>.
 */

// Headers
#include "game_actors.h"
#include "game_battle.h"
#include "game_enemyparty.h"
#include "game_interpreter_battle.h"
#include "game_party.h"
#include "game_switches.h"
#include "game_system.h"
#include "game_variables.h"
#include <lcf/reader_util.h>
#include "output.h"
#include "player.h"
#include "game_map.h"
#include "spriteset_battle.h"
#include <cassert>

enum BranchBattleSubcommand {
	eOptionBranchBattleElse = 1
};

Game_Interpreter_Battle::Game_Interpreter_Battle(Span<const lcf::rpg::TroopPage> pages)
	: Game_Interpreter(true), pages(pages), pages_state(pages.size() * 2, false)
{
}

bool Game_Interpreter_Battle::AreConditionsMet(const lcf::rpg::TroopPageCondition& condition) {
	if (!condition.flags.switch_a &&
		!condition.flags.switch_b &&
		!condition.flags.variable &&
		!condition.flags.turn &&
		!condition.flags.turn_enemy &&
		!condition.flags.turn_actor &&
		!condition.flags.fatigue &&
		!condition.flags.enemy_hp &&
		!condition.flags.actor_hp &&
		!condition.flags.command_actor
		) {
		// Pages without trigger are never run
		return false;
	}

	if (condition.flags.switch_a && !Main_Data::game_switches->Get(condition.switch_a_id))
		return false;

	if (condition.flags.switch_b && !Main_Data::game_switches->Get(condition.switch_b_id))
		return false;

	if (condition.flags.variable && !(Main_Data::game_variables->Get(condition.variable_id) >= condition.variable_value))
		return false;

	if (condition.flags.turn && !Game_Battle::CheckTurns(Game_Battle::GetTurn(), condition.turn_b, condition.turn_a))
		return false;

	if (condition.flags.turn_enemy &&
		!Game_Battle::CheckTurns((*Main_Data::game_enemyparty)[condition.turn_enemy_id].GetBattleTurn(),	condition.turn_enemy_b, condition.turn_enemy_a))
		return false;

	if (condition.flags.turn_actor &&
		!Game_Battle::CheckTurns(Main_Data::game_actors->GetActor(condition.turn_actor_id)->GetBattleTurn(), condition.turn_actor_b, condition.turn_actor_a))
		return false;

	if (condition.flags.fatigue) {
		int fatigue = Main_Data::game_party->GetFatigue();
		if (fatigue < condition.fatigue_min || fatigue > condition.fatigue_max)
			return false;
	}

	if (condition.flags.enemy_hp) {
		Game_Battler& enemy = (*Main_Data::game_enemyparty)[condition.enemy_id];
		int hp = enemy.GetHp();
		int hpmin = enemy.GetMaxHp() * condition.enemy_hp_min / 100;
		int hpmax = enemy.GetMaxHp() * condition.enemy_hp_max / 100;
		if (hp < hpmin || hp > hpmax)
			return false;
	}

	if (condition.flags.actor_hp) {
		Game_Actor* actor = Main_Data::game_actors->GetActor(condition.actor_id);
		int hp = actor->GetHp();
		int hpmin = actor->GetMaxHp() * condition.actor_hp_min / 100;
		int hpmax = actor->GetMaxHp() * condition.actor_hp_max / 100;
		if (hp < hpmin || hp > hpmax)
			return false;
	}

	if (condition.flags.command_actor &&
		condition.command_id != Main_Data::game_actors->GetActor(condition.command_actor_id)->GetLastBattleAction())
		return false;

	return true;
}

void Game_Interpreter_Battle::ResetPagesExecuted(const Game_Battler* battler) {
	if (battler == nullptr) {
		for (int i = 0; i < GetNumPages(); ++i) {
			SetHasPageExecuted(i + 1, false);
		}
	} else {
		for (const auto& page : pages) {
			const auto& condition = page.condition;

			// Reset pages without actor/enemy condition each turn
			if (!condition.flags.turn_actor &&
				!condition.flags.turn_enemy &&
				!condition.flags.command_actor) {
				SetHasPageExecuted(page.ID, false);
			}

			// Reset pages of specific actor after that actors turn
			if (HasPageExecuted(page.ID)) {
				if (battler->GetType() == Game_Battler::Type_Ally &&
						((condition.flags.turn_actor && Main_Data::game_actors->GetActor(condition.turn_actor_id) == battler) ||
						(condition.flags.command_actor && Main_Data::game_actors->GetActor(condition.command_actor_id) == battler))) {
					SetHasPageExecuted(page.ID, false);
				}
			}

			// Reset pages of specific enemy after that enemies turn
			if (battler->GetType() == Game_Battler::Type_Enemy &&
				condition.flags.turn_enemy &&
				(&((*Main_Data::game_enemyparty)[condition.turn_enemy_id]) == battler)) {
				SetHasPageExecuted(page.ID, false);
			}
		}
	}
}


// Execute Command.
bool Game_Interpreter_Battle::ExecuteCommand() {
	auto& frame = GetFrame();
	const auto& com = frame.commands[frame.current_command];

	switch (static_cast<Cmd>(com.code)) {
		case Cmd::CallCommonEvent:
			return CommandCallCommonEvent(com);
		case Cmd::ForceFlee:
			return CommandForceFlee(com);
		case Cmd::EnableCombo:
			return CommandEnableCombo(com);
		case Cmd::ChangeMonsterHP:
			return CommandChangeMonsterHP(com);
		case Cmd::ChangeMonsterMP:
			return CommandChangeMonsterMP(com);
		case Cmd::ChangeMonsterCondition:
			return CommandChangeMonsterCondition(com);
		case Cmd::ShowHiddenMonster:
			return CommandShowHiddenMonster(com);
		case Cmd::ChangeBattleBG:
			return CommandChangeBattleBG(com);
		case Cmd::ShowBattleAnimation_B:
			return CommandShowBattleAnimation(com);
		case Cmd::TerminateBattle:
			return CommandTerminateBattle(com);
		case Cmd::ConditionalBranch_B:
			return CommandConditionalBranchBattle(com);
		case Cmd::ElseBranch_B:
			return CommandElseBranchBattle(com);
		case Cmd::EndBranch_B:
			return CommandEndBranchBattle(com);
		default:
			return Game_Interpreter::ExecuteCommand();
	}
}

// Commands

bool Game_Interpreter_Battle::CommandCallCommonEvent(lcf::rpg::EventCommand const& com) {
	int evt_id = com.parameters[0];

	Game_CommonEvent* common_event = lcf::ReaderUtil::GetElement(Game_Map::GetCommonEvents(), evt_id);
	if (!common_event) {
		Output::Warning("CallCommonEvent: Can't call invalid common event {}", evt_id);
		return true;
	}

	Push(common_event);

	return true;
}

bool Game_Interpreter_Battle::CommandForceFlee(lcf::rpg::EventCommand const& com) {
	bool check = com.parameters[2] == 0;
	bool result = false;

	switch (com.parameters[0]) {
	case 0:
		if (!check || Game_Battle::GetBattleCondition() != lcf::rpg::System::BattleCondition_pincers) {
			_async_op = AsyncOp::MakeTerminateBattle(static_cast<int>(BattleResult::Escape));
			result = true;
		}
	    break;
	case 1:
		if (!check || Game_Battle::GetBattleCondition() != lcf::rpg::System::BattleCondition_surround) {
			for (int i = 0; i < Main_Data::game_enemyparty->GetBattlerCount(); ++i) {
				Game_Enemy& enemy = (*Main_Data::game_enemyparty)[i];
				enemy.Kill();
			}
			Game_Battle::SetNeedRefresh(true);
			result = true;
		}
	    break;
	case 2:
		if (!check || Game_Battle::GetBattleCondition() != lcf::rpg::System::BattleCondition_surround) {
			Game_Enemy& enemy = (*Main_Data::game_enemyparty)[com.parameters[1]];
			enemy.Kill();
			Game_Battle::SetNeedRefresh(true);
			result = true;
		}
	    break;
	}

	if (result) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Escape));
	}

	return true;
}

bool Game_Interpreter_Battle::CommandEnableCombo(lcf::rpg::EventCommand const& com) {
	int actor_id = com.parameters[0];

	if (!Main_Data::game_party->IsActorInParty(actor_id)) {
		return true;
	}

	int command_id = com.parameters[1];
	int multiple = com.parameters[2];

	Game_Actor* actor = Main_Data::game_actors->GetActor(actor_id);

	if (!actor) {
		Output::Warning("EnableCombo: Invalid actor ID {}", actor_id);
		return true;
	}

	actor->SetBattleCombo(command_id, multiple);

	return true;
}

bool Game_Interpreter_Battle::CommandChangeMonsterHP(lcf::rpg::EventCommand const& com) {
	int id = com.parameters[0];
	Game_Enemy& enemy = (*Main_Data::game_enemyparty)[id];
	bool lose = com.parameters[1] > 0;
	bool lethal = com.parameters[4] > 0;
	int hp = enemy.GetHp();

	if (enemy.IsDead())
		return true;

	int change = 0;
	switch (com.parameters[2]) {
	case 0:
		change = com.parameters[3];
	    break;
	case 1:
		change = Main_Data::game_variables->Get(com.parameters[3]);
	    break;
	case 2:
		change = com.parameters[3] * hp / 100;
	    break;
	}

	if (lose) {
		change = -change;
	}

	enemy.ChangeHp(change, lethal);

	if (enemy.IsDead()) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_EnemyKill));
		enemy.SetDeathTimer();
	}

	return true;
}

bool Game_Interpreter_Battle::CommandChangeMonsterMP(lcf::rpg::EventCommand const& com) {
	int id = com.parameters[0];
	Game_Enemy& enemy = (*Main_Data::game_enemyparty)[id];
	bool lose = com.parameters[1] > 0;
	int sp = enemy.GetSp();

	int change = 0;
	switch (com.parameters[2]) {
	case 0:
		change = com.parameters[3];
	    break;
	case 1:
		change = Main_Data::game_variables->Get(com.parameters[3]);
	    break;
	}

	if (lose)
		change = -change;

	sp += change;

	enemy.SetSp(sp);

	return true;
}

bool Game_Interpreter_Battle::CommandChangeMonsterCondition(lcf::rpg::EventCommand const& com) {
	Game_Enemy& enemy = (*Main_Data::game_enemyparty)[com.parameters[0]];
	bool remove = com.parameters[1] > 0;
	int state_id = com.parameters[2];
	if (remove) {
		// RPG_RT BUG: Monster dissapears immediately and doesn't animate death
		enemy.RemoveState(state_id, false);
	} else {
		enemy.AddState(state_id, true);
	}
	return true;
}

bool Game_Interpreter_Battle::CommandShowHiddenMonster(lcf::rpg::EventCommand const& com) {
	Game_Enemy& enemy = (*Main_Data::game_enemyparty)[com.parameters[0]];
	enemy.SetHidden(false);
	return true;
}

bool Game_Interpreter_Battle::CommandChangeBattleBG(lcf::rpg::EventCommand const& com) {
	Game_Battle::ChangeBackground(ToString(com.string));
	return true;
}

bool Game_Interpreter_Battle::CommandShowBattleAnimation(lcf::rpg::EventCommand const& com) {
	int animation_id = com.parameters[0];
	int target = com.parameters[1];
	bool waiting_battle_anim = com.parameters[2] != 0;
	bool allies = false;

	if (Player::IsRPG2k3()) {
		allies = com.parameters[3] != 0;
	}

	int frames = 0;

	if (target < 0) {
		std::vector<Game_Battler*> v;

		if (allies) {
			Main_Data::game_party->GetActiveBattlers(v);
		} else {
			Main_Data::game_enemyparty->GetActiveBattlers(v);
		}

		frames = Game_Battle::ShowBattleAnimation(animation_id, v, false);
	}
	else {
		Game_Battler* battler_target = nullptr;

		if (allies) {
			// Allies counted from 1
			target -= 1;
			if (target >= 0 && target < Main_Data::game_party->GetBattlerCount()) {
				battler_target = &(*Main_Data::game_party)[target];
			}
		}
		else {
			if (target < Main_Data::game_enemyparty->GetBattlerCount()) {
				battler_target = &(*Main_Data::game_enemyparty)[target];
			}
		}

		if (battler_target) {
			frames = Game_Battle::ShowBattleAnimation(animation_id, { battler_target });
		}
	}

	if (waiting_battle_anim) {
		_state.wait_time = frames;
	}

	return true;
}

bool Game_Interpreter_Battle::CommandTerminateBattle(lcf::rpg::EventCommand const& /* com */) {
	_async_op = AsyncOp::MakeTerminateBattle(static_cast<int>(BattleResult::Abort));
	return false;
}

// Conditional branch.
bool Game_Interpreter_Battle::CommandConditionalBranchBattle(lcf::rpg::EventCommand const& com) {
	bool result = false;
	int value1, value2;

	switch (com.parameters[0]) {
		case 0:
			// Switch
			result = Main_Data::game_switches->Get(com.parameters[1]) == (com.parameters[2] == 0);
			break;
		case 1:
			// Variable
			value1 = Main_Data::game_variables->Get(com.parameters[1]);
			if (com.parameters[2] == 0) {
				value2 = com.parameters[3];
			} else {
				value2 = Main_Data::game_variables->Get(com.parameters[3]);
			}
			switch (com.parameters[4]) {
				case 0:
					// Equal to
					result = (value1 == value2);
					break;
				case 1:
					// Greater than or equal
					result = (value1 >= value2);
					break;
				case 2:
					// Less than or equal
					result = (value1 <= value2);
					break;
				case 3:
					// Greater than
					result = (value1 > value2);
					break;
				case 4:
					// Less than
					result = (value1 < value2);
					break;
				case 5:
					// Different
					result = (value1 != value2);
					break;
			}
			break;
		case 2: {
			// Hero can act
			Game_Actor* actor = Main_Data::game_actors->GetActor(com.parameters[1]);

			if (!actor) {
				Output::Warning("ConditionalBranchBattle: Invalid actor ID {}", com.parameters[1]);
				// Use Else Branch
				SetSubcommandIndex(com.indent, 1);
				SkipToNextConditional({Cmd::ElseBranch_B, Cmd::EndBranch_B}, com.indent);
				return true;
			}

			result = actor->CanAct();
			break;
		}
		case 3:
			// Monster can act
			if (com.parameters[1] < Main_Data::game_enemyparty->GetBattlerCount()) {
				result = (*Main_Data::game_enemyparty)[com.parameters[1]].CanAct();
			}
			break;
		case 4:
			// Monster is the current target
			result = Game_Battle::GetEnemyTargetIndex() == com.parameters[1];
			break;
		case 5: {
			// Hero uses the ... command
			Game_Actor *actor = Main_Data::game_actors->GetActor(com.parameters[1]);

			if (!actor) {
				Output::Warning("ConditionalBranchBattle: Invalid actor ID {}", com.parameters[1]);
				// Use Else branch
				SetSubcommandIndex(com.indent, 1);
				SkipToNextConditional({Cmd::ElseBranch_B, Cmd::EndBranch_B}, com.indent);
				return true;
			}

			result = actor->GetLastBattleAction() == com.parameters[2];
			break;
		}
	}

	int sub_idx = subcommand_sentinel;
	if (!result) {
		sub_idx = eOptionBranchBattleElse;
		SkipToNextConditional({Cmd::ElseBranch_B, Cmd::EndBranch_B}, com.indent);
	}

	SetSubcommandIndex(com.indent, sub_idx);
	return true;
}

bool Game_Interpreter_Battle::CommandElseBranchBattle(lcf::rpg::EventCommand const& com) { //code 23310
	return CommandOptionGeneric(com, eOptionBranchBattleElse, {Cmd::EndBranch_B});
}

bool Game_Interpreter_Battle::CommandEndBranchBattle(lcf::rpg::EventCommand const& /* com */) { //code 23311
	return true;
}

