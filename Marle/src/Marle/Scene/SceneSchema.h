#pragma once

#include "Marle/Core.h"
#include "ComponentRegistry.h"
#include <string>

namespace Marle {

    // A JSON Schema (draft-07) for scene files, generated from the component registry so it always
    // lists exactly the components and fields the engine knows, with each field's type, default,
    // range and description. Editors and AI authors use it to see what is allowed. It follows the
    // loader's rules except for what a schema cannot express (entity names being unique, asset files
    // existing, keys repeated within an object); the loader is the authority. Needs no GL context.
    EXPORT Json GenerateSceneSchema(const ComponentRegistry& registry);

    // The schema as the text of a file: deterministic, two-space indentation, one trailing newline,
    // so a registry change shows up as a diff of the committed file.
    EXPORT std::string SceneSchemaToText(const ComponentRegistry& registry);

}
