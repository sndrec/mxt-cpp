#include "fzgx/content.h"

#include "../../catalog/machine_definitions.inc"
#include "../../catalog/track_animation_content.inc"
#include "../../catalog/track_manifests.inc"
#include "../../catalog/track_courses.inc"

static const fzgx_content_bundle FZGX_BUILTIN_ISO_BUNDLE = {
    FZGX_CONTENT_API_VERSION,
    (uint32_t)(sizeof(fzgx_iso_track_manifests) / sizeof(fzgx_iso_track_manifests[0])),
    (uint32_t)(sizeof(fzgx_generated_machine_definitions) /
               sizeof(fzgx_generated_machine_definitions[0])),
    (uint32_t)(sizeof(fzgx_generated_track_courses) / sizeof(fzgx_generated_track_courses[0])),
    (uint32_t)(sizeof(fzgx_generated_track_animations) /
               sizeof(fzgx_generated_track_animations[0])),
    fzgx_iso_track_manifests,
    fzgx_generated_machine_definitions,
    fzgx_generated_track_courses,
    fzgx_generated_track_animations,
};

const fzgx_content_bundle *fzgx_content_get_builtin_iso_bundle(void) {
  return &FZGX_BUILTIN_ISO_BUNDLE;
}
