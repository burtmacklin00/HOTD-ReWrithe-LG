// Horde Mode's placement table (horde4, user 2026-11-05: "go back to the video I showed you and just copy where the
// Remake places its additional creatures, and add the cut creatures for some of the new creatures"; docs/horde.md
// "Placements"). Read from the reference playthrough (THE HOUSE OF THE DEAD: Remake, Horde, arcade difficulty) stop by
// stop: where the Remake's additional creatures come from at the arcade's camera stops. A stop is found by the camera's
// place (x / z, within 12 units) and its look direction (x / z, within ~18 degrees) when it stops (camera_stopped 2);
// each line one creature: its delay after the stop (frames), its place on the floor (world space, from the stop's own
// picture: HOTD_HORDE_PICK), its type (-1: one of the chapter's walkers; a cut creature's type: with the arcade models
// only, else a walker). It faces the camera and walks in (start sub-state 2). The video's time of each stop is noted.
//  chapter, camera x, z, look x, z, delay, x, y, z, type
// Chapter 1 (the video 1:15-6:15; our default route; the Remake's mansion stops after the long corridor take another
// route there, where the arcade's own doors (the lanes) bring the copies)
// 1:20-1:27: the gate's field, three more across the middle while the arcade's two go for the man (one a cut creature)
{0, 0.0f, -1167.2f, 0.000f, 1.000f, 20, -48.6f, -25.0f, -1075.3f, -1},
{0, 0.0f, -1167.2f, 0.000f, 1.000f, 50, 20.6f, -25.0f, -1074.0f, -1},
{0, 0.0f, -1167.2f, 0.000f, 1.000f, 80, -3.2f, -25.0f, -1002.6f, 10},
// 1:37-1:42: after the man, a row of five from the back of the field (the camera turned left)
{0, 0.0f, -1167.2f, -0.547f, 0.837f, 10, -160.7f, -25.0f, -1090.1f, -1},
{0, 0.0f, -1167.2f, -0.547f, 0.837f, 30, -136.1f, -25.0f, -1067.6f, -1},
{0, 0.0f, -1167.2f, -0.547f, 0.837f, 50, -111.5f, -25.0f, -1045.1f, -1},
{0, 0.0f, -1167.2f, -0.547f, 0.837f, 70, -86.9f, -25.0f, -1022.6f, -1},
{0, 0.0f, -1167.2f, -0.547f, 0.837f, 90, -62.3f, -25.0f, -1000.1f, -1},
// 1:45-1:50: the courtyard's path, a group down the path (inside its fence, x > -100); 1:57-1:59 two more from the
// benches on the left
{0, -0.6f, -976.3f, -0.483f, 0.876f, 20, -92.0f, -25.0f, -850.0f, -1},
{0, -0.6f, -976.3f, -0.483f, 0.876f, 45, -88.0f, -25.0f, -878.0f, -1},
{0, -0.6f, -976.3f, -0.483f, 0.876f, 70, -70.0f, -25.0f, -860.0f, -1},
{0, -0.6f, -976.3f, -0.483f, 0.876f, 95, -75.9f, -25.0f, -915.2f, -1},
{0, -0.6f, -976.3f, -0.483f, 0.876f, 300, -90.0f, -25.0f, -930.0f, -1},
{0, -0.6f, -976.3f, -0.483f, 0.876f, 330, -58.1f, -25.0f, -901.3f, -1},
// 2:13-2:17: the fountain's square, from the left and the middle (a cut creature among them)
{0, -2.5f, -797.6f, -0.006f, 1.000f, 30, -71.4f, -25.0f, -632.7f, -1},
{0, -2.5f, -797.6f, -0.006f, 1.000f, 60, -4.8f, -25.0f, -631.8f, 52},
{0, -2.5f, -797.6f, -0.006f, 1.000f, 90, 61.9f, -25.0f, -631.0f, -1},
{0, -2.5f, -797.6f, -0.006f, 1.000f, 120, -26.4f, -25.0f, -704.5f, -1},
// 2:13-2:20: further on in the square
{0, 0.0f, -743.5f, -0.012f, 1.000f, 30, -67.1f, -25.0f, -582.9f, -1},
{0, 0.0f, -743.5f, -0.012f, 1.000f, 60, 30.7f, -25.0f, -581.8f, -1},
{0, 0.0f, -743.5f, -0.012f, 1.000f, 90, 63.2f, -25.0f, -581.4f, -1},
// 2:37-2:41: after the Hangedman takes Sophie, five across the square from the right and the front
{0, 16.9f, -643.4f, 0.359f, 0.933f, 10, 17.0f, -25.0f, -465.1f, -1},
{0, 16.9f, -643.4f, 0.359f, 0.933f, 40, 47.9f, -25.0f, -477.6f, -1},
{0, 16.9f, -643.4f, 0.359f, 0.933f, 70, 78.8f, -25.0f, -490.1f, 56},
{0, 16.9f, -643.4f, 0.359f, 0.933f, 100, 90.0f, -25.0f, -520.0f, -1},
{0, 16.9f, -643.4f, 0.359f, 0.933f, 130, 60.0f, -25.0f, -470.0f, -1},
// 3:00-3:04: the mansion's steps, two from the sides and one from the top
{0, 0.0f, -333.9f, -0.000f, 1.000f, 30, -42.8f, -25.0f, -122.4f, -1},
{0, 0.0f, -333.9f, -0.000f, 1.000f, 60, 42.7f, -25.0f, -122.4f, -1},
{0, 0.0f, -333.9f, -0.000f, 1.000f, 90, 0.0f, -25.0f, -200.6f, -1},
// 3:58-4:02: the long corridor upstairs, a file of three from its far end (one a cut creature)
{0, 0.0f, 42.7f, 0.000f, 1.000f, 30, 0.0f, 0.0f, 207.5f, -1},
{0, 0.0f, 42.7f, 0.000f, 1.000f, 90, 0.0f, 0.0f, 230.0f, 12},
{0, 0.0f, 42.7f, 0.000f, 1.000f, 150, 0.0f, 0.0f, 250.0f, -1},
// Chapter 2 (the video 8:00-11:45 takes the cellar / kitchen / dining hall / bar branch; our default route goes by the
// entrance hall, the paintings' corridor, the gallery, the balconies and the ruins' bridges. The Remake's pattern where
// the routes share a kind of place: groups along the open galleries and walkways, from the arcade's own directions;
// the rooms keep the arcade's doors: the lanes)
// (the pillared gallery: its pillars stand between its far side and the camera: the arcade's own two come from there, the
// lanes add theirs on their trails; no free walkers)
// (the outer balcony: its railing's bend stands across any straight walk in: the arcade's door only, the lanes)
// the walkway round the tower
{1, 734.1f, 420.0f, 1.000f, 0.020f, 30, 774.4f, -20.8f, 426.3f, -1},
{1, 734.1f, 420.0f, 1.000f, 0.020f, 100, 774.4f, -20.8f, 426.3f, 66},
// the ruins' second bridge over the water: a file along its middle (the first's gates block a straight walk)
{1, 1044.7f, 185.9f, 0.999f, -0.032f, 30, 1098.3f, -68.4f, 185.7f, -1},
{1, 1044.7f, 185.9f, 0.999f, -0.032f, 70, 1121.8f, -68.4f, 182.5f, 52},
{1, 1044.7f, 185.9f, 0.999f, -0.032f, 110, 1171.9f, -68.4f, 171.4f, -1},
// Chapter 3 (the video 16:05-: the labs, then the sewers branch; ours: the labs, the cells, the furnace). The Remake's
// lab rooms (17:10, 17:20) fill from the back of the room in groups of three or four; the furnace hall from its far side
// the first lab room past the entrance: from its back
{2, 96.5f, 206.7f, -0.185f, 0.983f, 30, 56.7f, -24.7f, 293.9f, -1},
{2, 96.5f, 206.7f, -0.185f, 0.983f, 60, 78.8f, -24.7f, 298.1f, -1},
{2, 96.5f, 206.7f, -0.185f, 0.983f, 90, 95.2f, -24.7f, 308.1f, 10},
// the shutter room
{2, 384.1f, 114.0f, 0.958f, -0.287f, 30, 479.3f, -45.7f, 110.1f, -1},
{2, 384.1f, 114.0f, 0.958f, -0.287f, 60, 480.8f, -45.7f, 107.1f, -1},
{2, 384.1f, 114.0f, 0.958f, -0.287f, 90, 486.0f, -45.7f, 133.4f, -1},
// the hazard-floor hall: a group across its far side
{2, 470.1f, 102.6f, 0.905f, 0.426f, 30, 575.6f, -45.7f, 180.4f, -1},
{2, 470.1f, 102.6f, 0.905f, 0.426f, 60, 590.5f, -45.7f, 163.3f, 56},
{2, 470.1f, 102.6f, 0.905f, 0.426f, 90, 593.4f, -45.7f, 134.2f, -1},
{2, 470.1f, 102.6f, 0.905f, 0.426f, 120, 533.3f, -45.7f, 134.0f, -1},
// (the furnace's walkway: its railing stands across a straight walk in: the lanes only)
// the furnace hall's last stand: its far side is behind the railing (x < 535): two more where the arcade's three stand
{2, 577.0f, 2087.1f, -1.000f, 0.002f, 60, 552.0f, -57.8f, 2062.0f, 52},
{2, 577.0f, 2087.1f, -1.000f, 0.002f, 120, 552.0f, -57.8f, 2112.0f, -1},
// Chapter 4 (the video 24:00-24:20 before the boss rush: the cave, then a hall filling with a crowd; ours: the cave's
// hall with the electric pillar, then the long corridor of doors whose pairs come from the sides)
// the cave's first stop: two more from its far side
{3, 5.0f, 121.5f, -0.295f, 0.956f, 40, -30.8f, 0.0f, 207.0f, -1},
{3, 5.0f, 121.5f, -0.295f, 0.956f, 80, -4.2f, 0.0f, 205.5f, 10},
// (the pillar's hall: its railings ring the floor in front: the lanes only)
// the long corridor: at each stop one more from its far end, beside the middle's lamps
{3, -126.3f, 1406.8f, 0.000f, 1.000f, 60, -118.3f, 0.0f, 1490.0f, -1},
{3, -126.3f, 1511.2f, 0.000f, 1.000f, 60, -118.3f, 0.0f, 1595.0f, 56},
{3, -126.3f, 1589.4f, 0.000f, 1.000f, 60, -118.3f, 0.0f, 1675.0f, -1},
{3, -126.3f, 1720.3f, 0.000f, 1.000f, 60, -134.3f, 0.0f, 1805.0f, 12},
