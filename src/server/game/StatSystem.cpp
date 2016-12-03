/*
 * Copyright (C) 2005-2008 MaNGOS <http://www.mangosproject.org/>
 *
 * Copyright (C) 2008 Trinity <http://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "Unit.h"
#include "Player.h"
#include "Pet.h"
#include "Creature.h"
#include "SharedDefines.h"
#include "SpellAuras.h"

/*#######################################
########                         ########
########   PLAYERS STAT SYSTEM   ########
########                         ########
#######################################*/

bool Player::UpdateStats(Stats stat)
{
    if(stat > STAT_SPIRIT)
        return false;

    // value = ((base_value * base_pct) + total_value) * total_pct
    float value  = GetTotalStatValue(stat);

    SetStat(stat, int32(value));

    if(stat == STAT_STAMINA || stat == STAT_INTELLECT)
    {
        Pet *pet = GetPet();
        if(pet)
            pet->UpdateStats(stat);
    }

    switch(stat)
    {
        case STAT_STRENGTH:
            UpdateAttackPowerAndDamage();
            UpdateShieldBlockValue();
            break;
        case STAT_AGILITY:
            UpdateArmor();
            UpdateAttackPowerAndDamage(true);
            if(GetClass() == CLASS_ROGUE || GetClass() == CLASS_HUNTER || (GetClass() == CLASS_DRUID && m_form == FORM_CAT))
                UpdateAttackPowerAndDamage();

            UpdateAllCritPercentages();
            UpdateDodgePercentage();
            break;

        case STAT_STAMINA:   UpdateMaxHealth(); break;
        case STAT_INTELLECT:
            UpdateMaxPower(POWER_MANA);
            UpdateAllSpellCritChances();
            UpdateAttackPowerAndDamage(true);               //SPELL_AURA_MOD_RANGED_ATTACK_POWER_OF_STAT_PERCENT, only intellect currently
            UpdateArmor();                                  //SPELL_AURA_MOD_RESISTANCE_OF_INTELLECT_PERCENT, only armor currently
            break;

        case STAT_SPIRIT:
            break;

        default:
            break;
    }
    UpdateSpellDamageAndHealingBonus();
    UpdateManaRegen();
    return true;
}

void Player::UpdateSpellDamageAndHealingBonus()
{
    // Magic damage modifiers implemented in Unit::SpellDamageBonusDone
    // This information for client side use only
    // Get healing bonus for all schools
    SetStatInt32Value(PLAYER_FIELD_MOD_HEALING_DONE_POS, SpellBaseHealingBonusDone(SPELL_SCHOOL_MASK_ALL));
    // Get damage bonus for all schools
    for(int i = SPELL_SCHOOL_HOLY; i < MAX_SPELL_SCHOOL; i++)
        SetStatInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS+i, SpellBaseDamageBonusDone(SpellSchoolMask(1 << i)));
}

bool Player::UpdateAllStats()
{
    for (int i = STAT_STRENGTH; i < MAX_STATS; i++)
    {
        float value = GetTotalStatValue(Stats(i));
        SetStat(Stats(i), (int32)value);
    }

    UpdateAttackPowerAndDamage();
    UpdateAttackPowerAndDamage(true);
    UpdateArmor();
    UpdateMaxHealth();

    for(int i = POWER_MANA; i < MAX_POWERS; i++)
        UpdateMaxPower(Powers(i));

    UpdateAllCritPercentages();
    UpdateAllSpellCritChances();
    UpdateDefenseBonusesMod();
    UpdateShieldBlockValue();
    UpdateSpellDamageAndHealingBonus();
    UpdateManaRegen();
    UpdateExpertise(BASE_ATTACK);
    UpdateExpertise(OFF_ATTACK);
    for (int i = SPELL_SCHOOL_NORMAL; i < MAX_SPELL_SCHOOL; i++)
        UpdateResistances(i);

    return true;
}

void Player::UpdateResistances(uint32 school)
{
    if(school > SPELL_SCHOOL_NORMAL)
    {
        float value  = GetTotalAuraModValue(UnitMods(UNIT_MOD_RESISTANCE_START + school));
        SetResistance(SpellSchools(school), int32(value));

        Pet *pet = GetPet();
        if(pet)
            pet->UpdateResistances(school);
    }
    else
        UpdateArmor();
}

void Player::UpdateArmor()
{
    float value = 0.0f;
    UnitMods unitMod = UNIT_MOD_ARMOR;

    value  = GetModifierValue(unitMod, BASE_VALUE);         // base armor (from items)
    value *= GetModifierValue(unitMod, BASE_PCT);           // armor percent from items
    
    // Enrage
    if (GetAura(5229,0))
    {
        switch(m_form)
        {
            case FORM_BEAR:
                value *= 0.84f;
                break;
            case FORM_DIREBEAR:
                value *= 0.73f;
                break;
            default:
                break;
        }
    }
    
    value += GetStat(STAT_AGILITY) * 2.0f;                  // armor bonus from stats
    value += GetModifierValue(unitMod, TOTAL_VALUE);

    //add dynamic flat mods
    AuraList const& mResbyIntellect = GetAurasByType(SPELL_AURA_MOD_RESISTANCE_OF_STAT_PERCENT);
    for(auto i : mResbyIntellect)
    {
        Modifier const* mod = i->GetModifier();
        if(mod->m_miscvalue & SPELL_SCHOOL_MASK_NORMAL)
            value += int32(GetStat(Stats(i->GetMiscBValue())) * i->GetModifierValue() / 100.0f);
    }

    value *= GetModifierValue(unitMod, TOTAL_PCT);

    SetArmor(int32(value));

    Pet *pet = GetPet();
    if(pet)
        pet->UpdateArmor();
}

float Player::GetHealthBonusFromStamina() const
{
    float stamina = GetStat(STAT_STAMINA);

    float baseStam = stamina < 20 ? stamina : 20;
    float moreStam = stamina - baseStam;

    return baseStam + (moreStam*10.0f);
}

float Player::GetManaBonusFromIntellect() const
{
    float intellect = GetStat(STAT_INTELLECT);

    float baseInt = intellect < 20 ? intellect : 20;
    float moreInt = intellect - baseInt;

    return baseInt + (moreInt*15.0f);
}

void Player::UpdateMaxHealth()
{
    UnitMods unitMod = UNIT_MOD_HEALTH;

    float value = GetModifierValue(unitMod, BASE_VALUE) + GetCreateHealth();
    value *= GetModifierValue(unitMod, BASE_PCT);
    value += GetModifierValue(unitMod, TOTAL_VALUE) + GetHealthBonusFromStamina();
    value *= GetModifierValue(unitMod, TOTAL_PCT);

    SetMaxHealth((uint32)value);
}

void Player::UpdateMaxPower(Powers power)
{
    UnitMods unitMod = UnitMods(UNIT_MOD_POWER_START + power);

    float bonusPower = (power == POWER_MANA) ? GetManaBonusFromIntellect() : 0;

    float value = GetModifierValue(unitMod, BASE_VALUE) + GetCreatePowers(power);
    value *= GetModifierValue(unitMod, BASE_PCT);
    value += GetModifierValue(unitMod, TOTAL_VALUE) +  bonusPower;
    value *= GetModifierValue(unitMod, TOTAL_PCT);

    SetMaxPower(power, uint32(value));
}

void Player::UpdateAttackPowerAndDamage(bool ranged )
{
    float val2 = 0.0f;
    float level = float(GetLevel());

    UnitMods unitMod = ranged ? UNIT_MOD_ATTACK_POWER_RANGED : UNIT_MOD_ATTACK_POWER;

    uint16 index = UNIT_FIELD_ATTACK_POWER;
    uint16 index_mod = UNIT_FIELD_ATTACK_POWER_MODS;
    uint16 index_mult = UNIT_FIELD_ATTACK_POWER_MULTIPLIER;

    if(ranged)
    {
        index = UNIT_FIELD_RANGED_ATTACK_POWER;
        index_mod = UNIT_FIELD_RANGED_ATTACK_POWER_MODS;
        index_mult = UNIT_FIELD_RANGED_ATTACK_POWER_MULTIPLIER;

        switch(GetClass())
        {
            case CLASS_HUNTER: 
                val2 = level * 2.0f + GetStat(STAT_AGILITY) - 10.0f;    
                break;
            case CLASS_ROGUE:  
                val2 = level        + GetStat(STAT_AGILITY) - 10.0f;    
                break;
            case CLASS_WARRIOR:
                val2 = level        + GetStat(STAT_AGILITY) - 10.0f;    
                break;
            case CLASS_DRUID:
                switch(m_form)
                {
                    case FORM_CAT:
                    case FORM_BEAR:
                    case FORM_DIREBEAR:
                        val2 = 0.0f; break;
                    default:
                        val2 = GetStat(STAT_AGILITY) - 10.0f; break;
                }
                break;
            default: val2 = GetStat(STAT_AGILITY) - 10.0f; break;
        }
    }
    else
    {
        switch(GetClass())
        {
            case CLASS_WARRIOR:
                val2 = level * 3.0f + GetStat(STAT_STRENGTH) * 2.0f - 20.0f;
                break;
            case CLASS_PALADIN:
                val2 = level * 3.0f + GetStat(STAT_STRENGTH) * 2.0f - 20.0f;
                break;
#ifdef LICH_KING
            case CLASS_DEATH_KNIGHT:
                val2 = level * 3.0f + GetStat(STAT_STRENGTH) * 2.0f - 20.0f;
                break;
#endif
            case CLASS_ROGUE:
                val2 = level * 2.0f + GetStat(STAT_STRENGTH) + GetStat(STAT_AGILITY) - 20.0f;
                break;
            case CLASS_HUNTER:
                val2 = level * 2.0f + GetStat(STAT_STRENGTH) + GetStat(STAT_AGILITY) - 20.0f;
                break;
            case CLASS_SHAMAN:
                val2 = level * 2.0f + GetStat(STAT_STRENGTH) + GetStat(STAT_AGILITY) - 20.0f;
                break;
            case CLASS_DRUID:
            {
                //Check if Predatory Strikes is skilled
                float mLevelMult = 0.0;
                switch(m_form)
                {
                    case FORM_CAT:
                    case FORM_BEAR:
                    case FORM_DIREBEAR:
                    case FORM_MOONKIN:
                    {
                        Unit::AuraList const& mDummy = GetAurasByType(SPELL_AURA_DUMMY);
                        for(auto itr : mDummy)
                        {
                            // Predatory Strikes
                            if (itr->GetSpellInfo()->SpellIconID == 1563)
                            {
                                mLevelMult = itr->GetModifier()->m_amount / 100.0f;
                                break;
                            }
                        }
                        break;
                    }
                }

                switch(m_form)
                {
                    case FORM_CAT:
                        val2 = GetLevel()*(mLevelMult+2.0f) + GetStat(STAT_STRENGTH)*2.0f + GetStat(STAT_AGILITY) - 20.0f; break;
                    case FORM_BEAR:
                    case FORM_DIREBEAR:
                        val2 = GetLevel()*(mLevelMult+3.0f) + GetStat(STAT_STRENGTH)*2.0f - 20.0f; break;
                    case FORM_MOONKIN:
                        val2 = GetLevel()*(mLevelMult+1.5f) + GetStat(STAT_STRENGTH)*2.0f - 20.0f; break;
                    default:
                        val2 = GetStat(STAT_STRENGTH)*2.0f - 20.0f; break;
                }
                break;
            }
            case CLASS_MAGE:    val2 =              GetStat(STAT_STRENGTH)                         - 10.0f; break;
            case CLASS_PRIEST:  val2 =              GetStat(STAT_STRENGTH)                         - 10.0f; break;
            case CLASS_WARLOCK: val2 =              GetStat(STAT_STRENGTH)                         - 10.0f; break;
        }
    }

    SetModifierValue(unitMod, BASE_VALUE, val2);

    float base_attPower  = GetModifierValue(unitMod, BASE_VALUE) * GetModifierValue(unitMod, BASE_PCT);
    float attPowerMod = GetModifierValue(unitMod, TOTAL_VALUE);

    //add dynamic flat mods
    if( ranged && (GetClassMask() & CLASSMASK_WAND_USERS)==0)
    {
        AuraList const& mRAPbyIntellect = GetAurasByType(SPELL_AURA_MOD_RANGED_ATTACK_POWER_OF_STAT_PERCENT);
        for(auto i : mRAPbyIntellect)
            attPowerMod += int32(GetStat(Stats(i->GetModifier()->m_miscvalue)) * i->GetModifierValue() / 100.0f);
    }
#ifdef LICH_KING
    else
    {
        AuraEffectList const& mAPbyStat = GetAuraEffectsByType(SPELL_AURA_MOD_ATTACK_POWER_OF_STAT_PERCENT);
        for (AuraEffectList::const_iterator i = mAPbyStat.begin(); i != mAPbyStat.end(); ++i)
            attPowerMod += CalculatePct(GetStat(Stats((*i)->GetMiscValue())), (*i)->GetAmount());

        AuraEffectList const& mAPbyArmor = GetAuraEffectsByType(SPELL_AURA_MOD_ATTACK_POWER_OF_ARMOR);
        for (AuraEffectList::const_iterator iter = mAPbyArmor.begin(); iter != mAPbyArmor.end(); ++iter)
            // always: ((*i)->GetModifier()->m_miscvalue == 1 == SPELL_SCHOOL_MASK_NORMAL)
            attPowerMod += int32(GetArmor() / (*iter)->GetAmount());
    }
#endif

    float attPowerMultiplier = GetModifierValue(unitMod, TOTAL_PCT) - 1.0f;

    SetUInt32Value(index, (uint32)base_attPower);           //UNIT_FIELD_(RANGED)_ATTACK_POWER field
    SetUInt32Value(index_mod, (uint32)attPowerMod);         //UNIT_FIELD_(RANGED)_ATTACK_POWER_MODS field
    SetFloatValue(index_mult, attPowerMultiplier);          //UNIT_FIELD_(RANGED)_ATTACK_POWER_MULTIPLIER field

    //automatically update weapon damage after attack power modification
    if(ranged)
    {
        UpdateDamagePhysical(RANGED_ATTACK);

        Pet *pet = GetPet();                                //update pet's AP
        if(pet && pet->getPetType() == HUNTER_PET)
            pet->UpdateAttackPowerAndDamage();
    }
    else
    {
        UpdateDamagePhysical(BASE_ATTACK);
        if(CanDualWield() && HaveOffhandWeapon())           //allow update offhand damage only if player knows DualWield Spec and has equipped offhand weapon
            UpdateDamagePhysical(OFF_ATTACK);
        if(GetClass() == CLASS_SHAMAN 
#ifdef LICH_KING
            || getClass() == CLASS_PALADIN) 
#endif
            )                      // mental quickness
            UpdateSpellDamageAndHealingBonus();

#ifdef LICH_KING
        if (pet && pet->IsPetGhoul()) // At melee attack power change for DK pet
            pet->UpdateAttackPowerAndDamage();

        if (guardian && guardian->IsSpiritWolf()) // At melee attack power change for Shaman feral spirit
            guardian->UpdateAttackPowerAndDamage();
#endif
    }
}

void Player::UpdateShieldBlockValue()
{
    SetUInt32Value(PLAYER_SHIELD_BLOCK, GetShieldBlockValue());
}

//TODO : This should not add attack power to wands
void Player::CalculateMinMaxDamage(WeaponAttackType attType, bool normalized, bool addTotalPct, float& minDamage, float& maxDamage, Unit* target)
{
    UnitMods unitMod;

    switch (attType)
    {
        case BASE_ATTACK:
        default:
            unitMod = UNIT_MOD_DAMAGE_MAINHAND;
            break;
        case OFF_ATTACK:
            unitMod = UNIT_MOD_DAMAGE_OFFHAND;
            break;
        case RANGED_ATTACK:
            unitMod = UNIT_MOD_DAMAGE_RANGED;
            break;
    }

    float attackSpeedMod = GetAPMultiplier(attType, normalized);

    float baseValue  = GetModifierValue(unitMod, BASE_VALUE) + GetTotalAttackPowerValue(attType, target) / 14.0f * attackSpeedMod;
    float basePct    = GetModifierValue(unitMod, BASE_PCT);
    float totalValue = GetModifierValue(unitMod, TOTAL_VALUE);
    float totalPct   = addTotalPct ? GetModifierValue(unitMod, TOTAL_PCT) : 1.0f;

    float weaponMinDamage = GetWeaponDamageRange(attType, MINDAMAGE);
    float weaponMaxDamage = GetWeaponDamageRange(attType, MAXDAMAGE);

    if (IsInFeralForm()) // check if player is druid and in cat or bear forms
    {
        uint8 lvl = GetLevel();
        if (lvl > 60)
            lvl = 60;

        weaponMinDamage = lvl * 0.85f * attackSpeedMod;
        weaponMaxDamage = lvl * 1.25f * attackSpeedMod;
    }
    else if (!CanUseAttackType(attType)) // check if player not in form but still can't use (disarm case)
    {
        // cannot use ranged/off attack, set values to 0
        if (attType != BASE_ATTACK)
        {
            minDamage = 0;
            maxDamage = 0;
            return;
        }
        weaponMinDamage = BASE_MINDAMAGE;
        weaponMaxDamage = BASE_MAXDAMAGE;
    }
    else if (attType == RANGED_ATTACK) // add ammo DPS to ranged damage
    {
        weaponMinDamage += GetAmmoDPS() * attackSpeedMod;
        weaponMaxDamage += GetAmmoDPS() * attackSpeedMod;
    }

    minDamage = ((weaponMinDamage + baseValue) * basePct + totalValue) * totalPct;
    maxDamage = ((weaponMaxDamage + baseValue) * basePct + totalValue) * totalPct;
}

void Player::UpdateDefenseBonusesMod()
{
    UpdateBlockPercentage();
    UpdateParryPercentage();
    UpdateDodgePercentage();
}

void Player::UpdateBlockPercentage()
{
    // No block
    float value = 0.0f;
    if(CanBlock())
    {
        // Base value
        value = 5.0f;
        // Modify value from defense skill
        value += (int32(GetDefenseSkillValue()) - int32(GetMaxSkillValueForLevel())) * 0.04f;
        // Increase from SPELL_AURA_MOD_BLOCK_PERCENT aura
        value += GetTotalAuraModifier(SPELL_AURA_MOD_BLOCK_PERCENT);
        // Increase from rating
        value += GetRatingBonusValue(CR_BLOCK);
        value = value < 0.0f ? 0.0f : value;
    }
    SetStatFloatValue(PLAYER_BLOCK_PERCENTAGE, value);
}

void Player::UpdateCritPercentage(WeaponAttackType attType)
{
    BaseModGroup modGroup;
    uint16 index;
    CombatRating cr;

    switch(attType)
    {
        case OFF_ATTACK:
            modGroup = OFFHAND_CRIT_PERCENTAGE;
            index = PLAYER_OFFHAND_CRIT_PERCENTAGE;
            cr = CR_CRIT_MELEE;
            break;
        case RANGED_ATTACK:
            modGroup = RANGED_CRIT_PERCENTAGE;
            index = PLAYER_RANGED_CRIT_PERCENTAGE;
            cr = CR_CRIT_RANGED;
            break;
        case BASE_ATTACK:
        default:
            modGroup = CRIT_PERCENTAGE;
            index = PLAYER_CRIT_PERCENTAGE;
            cr = CR_CRIT_MELEE;
            break;
    }

    float value = GetTotalPercentageModValue(modGroup) + GetRatingBonusValue(cr);
    // Modify crit from weapon skill and maximized defense skill of same level victim difference
    value += (int32(GetWeaponSkillValue(attType)) - int32(GetMaxSkillValueForLevel())) * 0.04f;
    value = value < 0.0f ? 0.0f : value;
    SetStatFloatValue(index, value);
}

void Player::UpdateAllCritPercentages()
{
    float value = GetMeleeCritFromAgility();

    SetBaseModValue(CRIT_PERCENTAGE, PCT_MOD, value);
    SetBaseModValue(OFFHAND_CRIT_PERCENTAGE, PCT_MOD, value);
    SetBaseModValue(RANGED_CRIT_PERCENTAGE, PCT_MOD, value);

    UpdateCritPercentage(BASE_ATTACK);
    UpdateCritPercentage(OFF_ATTACK);
    UpdateCritPercentage(RANGED_ATTACK);
}

void Player::UpdateParryPercentage()
{
    // No parry
    float value = 0.0f;
    if (CanParry())
    {
        // Base parry
        value  = 5.0f;
        // Modify value from defense skill
        value += (int32(GetDefenseSkillValue()) - int32(GetMaxSkillValueForLevel())) * 0.04f;
        // Parry from SPELL_AURA_MOD_PARRY_PERCENT aura
        value += GetTotalAuraModifier(SPELL_AURA_MOD_PARRY_PERCENT);
        // Parry from rating
        value += GetRatingBonusValue(CR_PARRY);
        value = value < 0.0f ? 0.0f : value;
    }
    SetStatFloatValue(PLAYER_PARRY_PERCENTAGE, value);
}

void Player::UpdateDodgePercentage()
{
    // Dodge from agility
    float value = GetDodgeFromAgility();
    // Modify value from defense skill
    value += (int32(GetDefenseSkillValue()) - int32(GetMaxSkillValueForLevel())) * 0.04f;
    // Dodge from SPELL_AURA_MOD_DODGE_PERCENT aura
    value += GetTotalAuraModifier(SPELL_AURA_MOD_DODGE_PERCENT);
    // Dodge from rating
    value += GetRatingBonusValue(CR_DODGE);
    value = value < 0.0f ? 0.0f : value;
    SetStatFloatValue(PLAYER_DODGE_PERCENTAGE, value);
}

void Player::UpdateSpellCritChance(uint32 school)
{
    // For normal school set zero crit chance
    if(school == SPELL_SCHOOL_NORMAL)
    {
        SetFloatValue(PLAYER_SPELL_CRIT_PERCENTAGE1, 0.0f);
        return;
    }
    // For others recalculate it from:
    float crit = 0.0f;
    // Crit from Intellect
    crit += GetSpellCritFromIntellect();
    // Increase crit from SPELL_AURA_MOD_SPELL_CRIT_CHANCE
    crit += GetTotalAuraModifier(SPELL_AURA_MOD_SPELL_CRIT_CHANCE);
    // Increase crit by school from SPELL_AURA_MOD_SPELL_CRIT_CHANCE_SCHOOL
    crit += GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_SPELL_CRIT_CHANCE_SCHOOL, 1<<school);
    // Increase crit from spell crit ratings
    crit += GetRatingBonusValue(CR_CRIT_SPELL);

    // Store crit value
    SetFloatValue(PLAYER_SPELL_CRIT_PERCENTAGE1 + school, crit);
}

void Player::UpdateAllSpellCritChances()
{
    for (int i = SPELL_SCHOOL_NORMAL; i < MAX_SPELL_SCHOOL; i++)
        UpdateSpellCritChance(i);
}

void Player::UpdateExpertise(WeaponAttackType attack)
{
    if(attack==RANGED_ATTACK)
        return;

    int32 expertise = int32(GetRatingBonusValue(CR_EXPERTISE));

    Item *weapon = GetWeaponForAttack(attack);

    AuraList const& expAuras = GetAurasByType(SPELL_AURA_MOD_EXPERTISE);
    for(auto expAura : expAuras)
    {
        // item neutral spell
        if(expAura->GetSpellInfo()->EquippedItemClass == -1)
            expertise += expAura->GetModifierValue();
        // item dependent spell
        else if(weapon && weapon->IsFitToSpellRequirements(expAura->GetSpellInfo()))
            expertise += expAura->GetModifierValue();
    }

    if(expertise < 0)
        expertise = 0;

    switch(attack)
    {
        case BASE_ATTACK: SetUInt32Value(PLAYER_EXPERTISE, expertise);         break;
        case OFF_ATTACK:  SetUInt32Value(PLAYER_OFFHAND_EXPERTISE, expertise); break;
        default: break;
    }
}

void Player::UpdateManaRegen()
{
    float Intellect = GetStat(STAT_INTELLECT);
    // Mana regen from spirit and intellect
    float power_regen = sqrt(Intellect) * OCTRegenMPPerSpirit();
    // Apply PCT bonus from SPELL_AURA_MOD_POWER_REGEN_PERCENT aura on spirit base regen
    power_regen *= GetTotalAuraMultiplierByMiscValue(SPELL_AURA_MOD_POWER_REGEN_PERCENT, POWER_MANA);

    // Mana regen from SPELL_AURA_MOD_POWER_REGEN aura
    float power_regen_mp5 = GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_POWER_REGEN, POWER_MANA) / 5.0f;

    // Get bonus from SPELL_AURA_MOD_MANA_REGEN_FROM_STAT aura
    AuraList const& regenAura = GetAurasByType(SPELL_AURA_MOD_MANA_REGEN_FROM_STAT);
    for(auto i : regenAura)
    {
        Modifier const* mod = i->GetModifier();
        power_regen_mp5 += GetStat(Stats(mod->m_miscvalue)) * i->GetModifierValue() / 500.0f;
    }

    // Bonus from some dummy auras
    AuraList const& mDummyAuras = GetAurasByType(SPELL_AURA_PERIODIC_DUMMY);
    for(auto mDummyAura : mDummyAuras)
        if(mDummyAura->GetId() == 34074)                          // Aspect of the Viper
        {
            power_regen_mp5 += mDummyAura->GetModifier()->m_amount * Intellect / 500.0f;
            // Add regen bonus from level in this dummy
            power_regen_mp5 += GetLevel() * 35 / 100;
        }

    // Set regen rate in cast state apply only on spirit based regen
    int32 modManaRegenInterrupt = GetTotalAuraModifier(SPELL_AURA_MOD_MANA_REGEN_INTERRUPT);
    if (modManaRegenInterrupt > 100)
        modManaRegenInterrupt = 100;
    SetStatFloatValue(PLAYER_FIELD_MOD_MANA_REGEN_INTERRUPT, power_regen_mp5 + power_regen * modManaRegenInterrupt / 100.0f);

    SetStatFloatValue(PLAYER_FIELD_MOD_MANA_REGEN, power_regen_mp5 + power_regen);
}

void Player::_ApplyAllStatBonuses()
{
    SetCanModifyStats(false);

    _ApplyAllAuraMods();
    _ApplyAllItemMods();

    SetCanModifyStats(true);

    UpdateAllStats();
}

void Player::_RemoveAllStatBonuses()
{
    SetCanModifyStats(false);

    _RemoveAllItemMods();
    _RemoveAllAuraMods();

    SetCanModifyStats(true);

    UpdateAllStats();
}

/*#######################################
########                         ########
########    MOBS STAT SYSTEM     ########
########                         ########
#######################################*/

bool Creature::UpdateStats(Stats /*stat*/)
{
    return true;
}

bool Creature::UpdateAllStats()
{
    UpdateMaxHealth();
    UpdateAttackPowerAndDamage();
    UpdateAttackPowerAndDamage(true);

    for(int i = POWER_MANA; i < MAX_POWERS; ++i)
        UpdateMaxPower(Powers(i));

    for(int i = SPELL_SCHOOL_NORMAL; i < MAX_SPELL_SCHOOL; ++i)
        UpdateResistances(i);

    return true;
}

void Creature::UpdateResistances(uint32 school)
{
    if(school > SPELL_SCHOOL_NORMAL)
    {
        float value  = GetTotalAuraModValue(UnitMods(UNIT_MOD_RESISTANCE_START + school));
        SetResistance(SpellSchools(school), int32(value));
    }
    else
        UpdateArmor();
}

void Creature::UpdateArmor()
{
    float value = GetTotalAuraModValue(UNIT_MOD_ARMOR);
    SetArmor(int32(value));
}

void Creature::UpdateMaxHealth()
{
    float value = GetTotalAuraModValue(UNIT_MOD_HEALTH);
    SetMaxHealth((uint32)value);
}

void Creature::UpdateMaxPower(Powers power)
{
    UnitMods unitMod = UnitMods(UNIT_MOD_POWER_START + power);

    float value  = GetTotalAuraModValue(unitMod);
    SetMaxPower(power, uint32(value));
}

void Creature::UpdateAttackPowerAndDamage(bool ranged)
{
    UnitMods unitMod = ranged ? UNIT_MOD_ATTACK_POWER_RANGED : UNIT_MOD_ATTACK_POWER;

    uint16 index = UNIT_FIELD_ATTACK_POWER;
    uint16 indexMod = UNIT_FIELD_ATTACK_POWER_MODS;
    uint16 indexMulti = UNIT_FIELD_ATTACK_POWER_MULTIPLIER;

    if (ranged)
    {
        index = UNIT_FIELD_RANGED_ATTACK_POWER;
        indexMod = UNIT_FIELD_RANGED_ATTACK_POWER_MODS;
        indexMulti = UNIT_FIELD_RANGED_ATTACK_POWER_MULTIPLIER;
    }

    float baseAttackPower       = GetModifierValue(unitMod, BASE_VALUE) * GetModifierValue(unitMod, BASE_PCT);
    float attackPowerMod        = GetModifierValue(unitMod, TOTAL_VALUE);
    float attackPowerMultiplier = GetModifierValue(unitMod, TOTAL_PCT) - 1.0f;

    SetInt32Value(index, uint32(baseAttackPower));      // UNIT_FIELD_(RANGED)_ATTACK_POWER
    SetInt32Value(indexMod, uint32(attackPowerMod));    // UNIT_FIELD_(RANGED)_ATTACK_POWER_MODS
    SetFloatValue(indexMulti, attackPowerMultiplier);   // UNIT_FIELD_(RANGED)_ATTACK_POWER_MULTIPLIER

    // automatically update weapon damage after attack power modification
    if (ranged)
        UpdateDamagePhysical(RANGED_ATTACK);
    else
    {
        UpdateDamagePhysical(BASE_ATTACK);
        UpdateDamagePhysical(OFF_ATTACK);
    }
}


void Creature::CalculateMinMaxDamage(WeaponAttackType attType, bool normalized, bool addTotalPct, float& minDamage, float& maxDamage, Unit* target)
{
    float variance = 1.0f;
    UnitMods unitMod;
    switch (attType)
    {
        case BASE_ATTACK:
        default:
            variance = GetCreatureTemplate()->BaseVariance;
            unitMod = UNIT_MOD_DAMAGE_MAINHAND;
            break;
        case OFF_ATTACK:
            variance = GetCreatureTemplate()->BaseVariance;
            unitMod = UNIT_MOD_DAMAGE_OFFHAND;
            break;
        case RANGED_ATTACK:
            variance = GetCreatureTemplate()->RangeVariance;
            unitMod = UNIT_MOD_DAMAGE_RANGED;
            break;
    }

    if (attType == OFF_ATTACK && !HaveOffhandWeapon())
    {
        minDamage = 0.0f;
        maxDamage = 0.0f;
        return;
    }

    float weaponMinDamage = GetWeaponDamageRange(attType, MINDAMAGE);
    float weaponMaxDamage = GetWeaponDamageRange(attType, MAXDAMAGE);

    if (!CanUseAttackType(attType)) // disarm case
    {
        weaponMinDamage = 0.0f;
        weaponMaxDamage = 0.0f;
    }

    /*Custom formula here :
     $minDamage = ( $baseStats["damagebase"] + ($attackpower / 14.0 * $attackSpeedMulti) ) * $creatureModifiers["damage"];
     $maxDamage = $minDamage * (1 + variance);

     weapon min/max damage being already set in Creature::SelectLevel(), formula is a bit changed here, variance being applied 
     to base damage and to attack power separately.

     This formula isn't verified

     $minDamage = ( mindmg + ($attackpower / 14.0 * $attackSpeedMulti ) ) * $creatureModifiers["damage"];
     $maxDamage = ( maxdmg + ($attackpower / 14.0 * $attackSpeedMulti * (1+variance) ) ) * $creatureModifiers["damage"];

     */

    float attackPower      = GetTotalAttackPowerValue(attType, target);
    float attackSpeedMulti = GetAPMultiplier(attType, normalized);
    float baseValue        = GetModifierValue(unitMod, BASE_VALUE) + (attackPower / 14.0f) * attackSpeedMulti;
    float basePct          = GetModifierValue(unitMod, BASE_PCT);
    float totalValue       = GetModifierValue(unitMod, TOTAL_VALUE);
    float totalPct         = addTotalPct ? GetModifierValue(unitMod, TOTAL_PCT) : 1.0f;
    float dmgMultiplier    = GetCreatureTemplate()->ModDamage; // = ModDamage * _GetDamageMod(rank);

    minDamage = ((weaponMinDamage + baseValue) * dmgMultiplier * basePct + totalValue) * totalPct;
    maxDamage = ((weaponMaxDamage + baseValue * (1.0f + variance)) * dmgMultiplier * basePct + totalValue) * totalPct;
}

/*#######################################
########                         ########
########    PETS STAT SYSTEM     ########
########                         ########
#######################################*/

#define ENTRY_IMP               416
#define ENTRY_VOIDWALKER        1860
#define ENTRY_SUCCUBUS          1863
#define ENTRY_FELHUNTER         417
#define ENTRY_FELGUARD          17252
#define ENTRY_WATER_ELEMENTAL   510
#define ENTRY_TREANT            1964
#define ENTRY_FIRE_ELEMENTAL    15438
#define ENTRY_SHADOWFIEND       19668

bool Pet::UpdateStats(Stats stat)
{
    if(stat > STAT_SPIRIT)
        return false;

    // value = ((base_value * base_pct) + total_value) * total_pct
    float value  = GetTotalStatValue(stat);

    Unit *owner = GetOwner();
    if ( stat == STAT_STAMINA )
    {
        if (owner && (getPetType() == HUNTER_PET || owner->GetClass() == CLASS_WARLOCK))
            value += float(owner->GetStat(stat)) * 0.3f;
    }
                                                            //warlock's and mage's pets gain 30% of owner's intellect
    else if ( stat == STAT_INTELLECT && getPetType() == SUMMON_PET )
    {
        if(owner && (owner->GetClass() == CLASS_WARLOCK || owner->GetClass() == CLASS_MAGE) )
            value += float(owner->GetStat(stat)) * 0.3f;
    }

    SetStat(stat, int32(value));

    switch(stat)
    {
        case STAT_STRENGTH:         UpdateAttackPowerAndDamage();        break;
        case STAT_AGILITY:          UpdateArmor();                       break;
        case STAT_STAMINA:          UpdateMaxHealth();                   break;
        case STAT_INTELLECT:        UpdateMaxPower(POWER_MANA);          break;
        case STAT_SPIRIT:
        default:
            break;
    }

    return true;
}

bool Pet::UpdateAllStats()
{
    for (int i = STAT_STRENGTH; i < MAX_STATS; i++)
        UpdateStats(Stats(i));

    for(int i = POWER_MANA; i < MAX_POWERS; i++)
        UpdateMaxPower(Powers(i));

    for (int i = SPELL_SCHOOL_NORMAL; i < MAX_SPELL_SCHOOL; i++)
        UpdateResistances(i);

    return true;
}

void Pet::UpdateResistances(uint32 school)
{
    if(school > SPELL_SCHOOL_NORMAL)
    {
        float value  = GetTotalAuraModValue(UnitMods(UNIT_MOD_RESISTANCE_START + school));

        Unit *owner = GetOwner();
        // hunter and warlock pets gain 40% of owner's resistance
        if(owner && (getPetType() == HUNTER_PET || (getPetType() == SUMMON_PET && owner->GetClass() == CLASS_WARLOCK)))
            value += float(CalculatePct(owner->GetResistance(SpellSchools(school)), 40));

        SetResistance(SpellSchools(school), int32(value));
    }
    else
        UpdateArmor();
}

void Pet::UpdateArmor()
{
    float value = 0.0f;
    float bonus_armor = 0.0f;
    UnitMods unitMod = UNIT_MOD_ARMOR;

    Unit *owner = GetOwner();
    // hunter and warlock pets gain 35% of owner's armor value
    if(owner && (getPetType() == HUNTER_PET || (getPetType() == SUMMON_PET && owner->GetClass() == CLASS_WARLOCK)))
        bonus_armor = 0.35f * float(owner->GetArmor());

    value  = GetModifierValue(unitMod, BASE_VALUE);
    value *= GetModifierValue(unitMod, BASE_PCT);
    value += GetStat(STAT_AGILITY) * 2.0f;
    value += GetModifierValue(unitMod, TOTAL_VALUE) + bonus_armor;
    value *= GetModifierValue(unitMod, TOTAL_PCT);

    SetArmor(int32(value));
}

void Pet::UpdateMaxHealth()
{
    UnitMods unitMod = UNIT_MOD_HEALTH;
    float stamina = GetStat(STAT_STAMINA) - GetCreateStat(STAT_STAMINA);

    float value   = GetModifierValue(unitMod, BASE_VALUE) + GetCreateHealth();
    value  *= GetModifierValue(unitMod, BASE_PCT);
    value  += GetModifierValue(unitMod, TOTAL_VALUE) + stamina * 10.0f;
    value  *= GetModifierValue(unitMod, TOTAL_PCT);

    SetMaxHealth((uint32)value);
}

void Pet::UpdateMaxPower(Powers power)
{
    UnitMods unitMod = UnitMods(UNIT_MOD_POWER_START + power);

    float addValue = (power == POWER_MANA) ? GetStat(STAT_INTELLECT) - GetCreateStat(STAT_INTELLECT) : 0.0f;

    float value  = GetModifierValue(unitMod, BASE_VALUE) + GetCreatePowers(power);
    value *= GetModifierValue(unitMod, BASE_PCT);
    value += GetModifierValue(unitMod, TOTAL_VALUE) +  addValue * 15.0f;
    value *= GetModifierValue(unitMod, TOTAL_PCT);

    SetMaxPower(power, uint32(value));
}

void Pet::UpdateAttackPowerAndDamage(bool ranged)
{
    if(ranged)
        return;

    float val = 0.0f;
    float bonusAP = 0.0f;
    UnitMods unitMod = UNIT_MOD_ATTACK_POWER;

    if(GetEntry() == 416)                                   // imp's attack power
        val = GetStat(STAT_STRENGTH) - 10.0f;
    else
        val = 2 * GetStat(STAT_STRENGTH) - 20.0f;

    Unit* owner = GetOwner();
    if( owner && owner->GetTypeId()==TYPEID_PLAYER)
    {
        if(getPetType() == HUNTER_PET)                      //hunter pets benefit from owner's attack power
        {
            bonusAP = owner->GetTotalAttackPowerValue(RANGED_ATTACK) * 0.22f;
            SetBonusDamage( int32(owner->GetTotalAttackPowerValue(RANGED_ATTACK) * 0.125f));
        }
        //demons benefit from warlocks shadow or fire damage
        else if(getPetType() == SUMMON_PET && owner->GetClass() == CLASS_WARLOCK)
        {
            int32 fire  = int32(owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + SPELL_SCHOOL_FIRE)) - owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + SPELL_SCHOOL_FIRE);
            int32 shadow = int32(owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + SPELL_SCHOOL_SHADOW)) - owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + SPELL_SCHOOL_SHADOW);
            int32 maximum  = (fire > shadow) ? fire : shadow;
            if(maximum < 0)
                maximum = 0;
            SetBonusDamage( int32(maximum * 0.15f));
            bonusAP = maximum * 0.57f;
        }
        //water elementals benefit from mage's frost damage
        else if(getPetType() == SUMMON_PET && owner->GetClass() == CLASS_MAGE)
        {
            int32 frost = int32(owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + SPELL_SCHOOL_FROST)) - owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + SPELL_SCHOOL_FROST);
            if(frost < 0)
                frost = 0;
            SetBonusDamage( int32(frost * 0.4f));
        }
    }

    SetModifierValue(UNIT_MOD_ATTACK_POWER, BASE_VALUE, val + bonusAP);

    //in BASE_VALUE of UNIT_MOD_ATTACK_POWER for creatures we store data of meleeattackpower field in DB
    float base_attPower  = GetModifierValue(unitMod, BASE_VALUE) * GetModifierValue(unitMod, BASE_PCT);
    float attPowerMod = GetModifierValue(unitMod, TOTAL_VALUE);
    float attPowerMultiplier = GetModifierValue(unitMod, TOTAL_PCT) - 1.0f;

    //UNIT_FIELD_(RANGED)_ATTACK_POWER field
    SetUInt32Value(UNIT_FIELD_ATTACK_POWER, (uint32)base_attPower);
    //UNIT_FIELD_(RANGED)_ATTACK_POWER_MODS field
    SetUInt32Value(UNIT_FIELD_ATTACK_POWER_MODS, (uint32)attPowerMod);
    //UNIT_FIELD_(RANGED)_ATTACK_POWER_MULTIPLIER field
    SetFloatValue(UNIT_FIELD_ATTACK_POWER_MULTIPLIER, attPowerMultiplier);

    //automatically update weapon damage after attack power modification
    UpdateDamagePhysical(BASE_ATTACK);
}
/*
void Pet::UpdateDamagePhysical(WeaponAttackType attType)
{
    if(attType > BASE_ATTACK)
        return;

    float bonusDamage = 0.0f;
    if(Unit* owner = GetOwner())
    {
        //force of nature
        if(GetEntry() == ENTRY_TREANT)
        {
            int32 spellDmg = int32(owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + SPELL_SCHOOL_NATURE)) - owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + SPELL_SCHOOL_NATURE);
            if(spellDmg > 0)
                bonusDamage = spellDmg * 0.09f;
        }
        //greater fire elemental
        else if(GetEntry() == ENTRY_FIRE_ELEMENTAL)
        {
            if(Unit* shaman = owner->GetOwner())
            {
                int32 spellDmg = int32(shaman->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + SPELL_SCHOOL_FIRE)) - shaman->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + SPELL_SCHOOL_FIRE);
                if(spellDmg > 0)
                    bonusDamage = spellDmg * 0.4f;
            }
        }
        // shadowfiend 65.7% per 10 hits so 6.57 per hit
        else if(GetEntry() == ENTRY_SHADOWFIEND)
        {
            if(Unit *owner = GetOwner())
            {
                int32 sDamage = int32(owner->SpellBaseDamageBonusDone(SPELL_SCHOOL_MASK_SHADOW));
                if(sDamage > 0)
                    bonusDamage = sDamage * 0.0657f;
                // 2 parts T4 DPS Bonus: should be handled in future table spell_dbc
                if (owner->HasAuraEffect(37570))
                    SetStat(STAT_STAMINA, GetStat(STAT_STAMINA)+75);
            }
        }
    }

    UnitMods unitMod = UNIT_MOD_DAMAGE_MAINHAND;

    float att_speed = float(GetAttackTime(BASE_ATTACK))/1000.0f;

    float base_value  = GetModifierValue(unitMod, BASE_VALUE) + GetTotalAttackPowerValue(attType)/ 14.0f * att_speed  + bonusDamage;
    float base_pct    = GetModifierValue(unitMod, BASE_PCT);
    float total_value = GetModifierValue(unitMod, TOTAL_VALUE);
    float total_pct   = GetModifierValue(unitMod, TOTAL_PCT);

    float weapon_mindamage = GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE);
    float weapon_maxdamage = GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE);

    float mindamage = ((base_value + weapon_mindamage) * base_pct + total_value) * total_pct;
    float maxdamage = ((base_value + weapon_maxdamage) * base_pct + total_value) * total_pct;

    //  Pet's base damage changes depending on happiness
    if (getPetType() == HUNTER_PET && attType == BASE_ATTACK)
    {
        switch(GetHappinessState())
        {
            case HAPPY:
                // 125% of normal damage
                mindamage = mindamage * 1.25;
                maxdamage = maxdamage * 1.25;
                break;
            case CONTENT:
                // 100% of normal damage, nothing to modify
                break;
            case UNHAPPY:
                // 75% of normal damage
                mindamage = mindamage * 0.75;
                maxdamage = maxdamage * 0.75;
                break;
        }
    }

    SetStatFloatValue(UNIT_FIELD_MINDAMAGE, mindamage);
    SetStatFloatValue(UNIT_FIELD_MAXDAMAGE, maxdamage);
}
*/
void Pet::CalculateMinMaxDamage(WeaponAttackType attType, bool normalized, bool addTotalPct, float& minDamage, float& maxDamage, Unit* target)
{
    if(attType > BASE_ATTACK)
        return;

    float variance = 1.0f;
    variance = GetCreatureTemplate()->BaseVariance;

    float bonusDamage = 0.0f;
    if(Unit* owner = GetOwner())
    {
        //force of nature
        if(GetEntry() == 1964)
        {
            int32 spellDmg = int32(owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + SPELL_SCHOOL_NATURE)) - owner->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + SPELL_SCHOOL_NATURE);
            if(spellDmg > 0)
                bonusDamage = spellDmg * 0.09f;
        }
        //greater fire elemental
        else if(GetEntry() == 15438)
        {
            if(Unit* shaman = owner->GetOwner())
            {
                int32 spellDmg = int32(shaman->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + SPELL_SCHOOL_FIRE)) - shaman->GetUInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG + SPELL_SCHOOL_FIRE);
                if(spellDmg > 0)
                    bonusDamage = spellDmg * 0.4f;
            }
        }
        // shadowfiend 65.7% per 10 hits so 6.57 per hit
        else if(GetEntry() == 19668)
        {
            if(Unit *owner = GetOwner())
            {
                int32 sDamage = int32(owner->SpellBaseDamageBonusDone(SPELL_SCHOOL_MASK_SHADOW));
                if(sDamage > 0)
                    bonusDamage = sDamage * 0.0657f;
                // 2 parts T4 DPS Bonus: should be handled in future table spell_dbc
                if (owner->HasAuraEffect(37570))
                    SetStat(STAT_STAMINA, GetStat(STAT_STAMINA)+75);
            }
        }
    }

    UnitMods unitMod = UNIT_MOD_DAMAGE_MAINHAND;

    float att_speed = float(GetAttackTime(BASE_ATTACK))/1000.0f;

    float base_value  = GetModifierValue(unitMod, BASE_VALUE) + GetTotalAttackPowerValue(attType, target)/ 14.0f * att_speed  + bonusDamage;
    float base_pct    = GetModifierValue(unitMod, BASE_PCT);
    float total_value = GetModifierValue(unitMod, TOTAL_VALUE);
    float total_pct   = GetModifierValue(unitMod, TOTAL_PCT);

    float weapon_mindamage = GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE);
    float weapon_maxdamage = GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE);

    minDamage = ((base_value + weapon_mindamage) * base_pct + total_value) * total_pct;
    maxDamage = ((base_value + weapon_maxdamage) * base_pct + total_value) * total_pct;

    //  Pet's base damage changes depending on happiness
    if (getPetType() == HUNTER_PET && attType == BASE_ATTACK)
    {
        switch(GetHappinessState())
        {
            case HAPPY:
                // 125% of normal damage
                minDamage = minDamage * 1.25;
                maxDamage = maxDamage * 1.25;
                break;
            case CONTENT:
                // 100% of normal damage, nothing to modify
                break;
            case UNHAPPY:
                // 75% of normal damage
                minDamage = minDamage * 0.75;
                maxDamage = maxDamage * 0.75;
                break;
        }
    }
}