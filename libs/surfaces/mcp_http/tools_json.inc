/* Master tools/list schema — composed of per-domain fragments under
 * schemas/. Each schemas/<domain>.inc is a raw-string literal that the
 * compiler concatenates with its neighbours. Edit individual domain files
 * to add or modify tools; this file just stitches them together.
 */
#include "schemas/header.inc"
#include "schemas/session.inc"
#include "schemas/transport.inc"
#include "schemas/markers.inc"
#include "schemas/tracks.inc"
#include "schemas/track.inc"
#include "schemas/regions.inc"
#include "schemas/plugins.inc"
#include "schemas/midi.inc"
#include "schemas/tempo.inc"
#include "schemas/footer.inc"
