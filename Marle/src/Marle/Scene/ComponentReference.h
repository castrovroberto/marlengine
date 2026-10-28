#pragma once

#include "Marle/Core.h"
#include "ComponentRegistry.h"
#include <string>

namespace Marle {

    // What the registry knows about one component, as data: the one description that
    // `marle-scene describe` prints and the generated component table is made from, so they cannot
    // drift apart. Fields keep their declaration order. `min`, `max`, `one_of` are left out when the
    // field has none, and `asset_path`, `entity_reference` and `required` are always there.
    //
    //   { "name", "description", "behavior",
    //     "fields": [ { "name", "description", "type", "default", "required", "asset_path",
    //                   "entity_reference", ["min"], ["max"], ["one_of"] } ] }
    EXPORT Json DescribeComponent(const ComponentInfo& component);

    // The same description for people: name, description, then one block per field.
    EXPORT std::string DescribeComponentText(const ComponentInfo& component);

}
