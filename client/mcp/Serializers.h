/*
 * Serializers.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#pragma once

#include <string>
#include <vector>

class JsonNode;
class PlayerColor;
class CGHeroInstance;
class CGTownInstance;
namespace battle { class Unit; }
namespace spells { class Spell; }
class Artifact;
class Skill;
class CArtifactInstance;
struct Component;

PlayerColor parsePlayerColor(const std::string & name);

JsonNode getFullGameConfig();
JsonNode heroToJson(const CGHeroInstance * h);
JsonNode townToJson(const CGTownInstance * t);
JsonNode artifactToJson(const Artifact * a);
JsonNode spellToJson(const spells::Spell * s);
JsonNode skillToJson(const Skill * s);
JsonNode artifactInstanceToJson(const CArtifactInstance * ai);
JsonNode heroDetailsToJson(const CGHeroInstance * h);
JsonNode townDetailsToJson(const CGTownInstance * t);
JsonNode battleUnitToJson(const battle::Unit * u);

/// Renders a reward/dialog Component (as seen in BlockingDialog, HeroLevelUp, ...) into a
/// JSON description an LLM can act on: component kind, referenced entity id/code, and value.
JsonNode componentToJson(const Component & c);
JsonNode componentsToJson(const std::vector<Component> & components);
