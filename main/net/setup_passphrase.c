/* SPDX-License-Identifier: LicenseRef-FNCL-1.1
 * Copyright (c) 2026 andre68723 -- file added in the andre68723/BETTA-HA-PANEL fork, see docs/release-notes.md
 */
#include "net/setup_passphrase.h"

#include <stdio.h>

/* 1024 common English words of 4 to 6 lower-case ASCII letters (a power of two, so the reduction of a
 * random number is unbiased). None contains an umlaut, which a phone keyboard would make awkward. */
static const char *const s_words[SETUP_PASSPHRASE_WORD_COUNT] = {
    "acorn", "adobe", "alarm", "album", "alder", "alley", "almond", "alpine", "amber", "anchor", "angle",
    "apple", "apron", "arrow", "aspen", "atlas", "attic", "autumn", "avenue", "badge", "bagel", "baker",
    "bamboo", "banjo", "barley", "barn", "basil", "basket", "beach", "beacon", "beaver", "bench", "berry",
    "birch", "bloom", "boat", "bonus", "border", "bottle", "branch", "bread", "breeze", "brick", "bridge",
    "bronze", "brook", "broom", "bubble", "bucket", "bundle", "butter", "button", "cabin", "cable", "cactus",
    "camel", "camera", "candle", "canoe", "canvas", "canyon", "carbon", "carpet", "carrot", "castle",
    "cedar", "cellar", "cherry", "chess", "circle", "citrus", "clay", "cliff", "clock", "cloud", "clover",
    "cobalt", "cocoa", "coffee", "comet", "copper", "coral", "cotton", "cougar", "cradle", "crater", "creek",
    "crown", "daisy", "dancer", "dawn", "delta", "desert", "dinner", "dragon", "drift", "drum", "eagle",
    "earth", "echo", "elbow", "ember", "engine", "fabric", "falcon", "fennel", "fern", "ferry", "fiddle",
    "field", "finch", "fjord", "flame", "flint", "flower", "forest", "fossil", "frost", "garden", "garlic",
    "ginger", "globe", "goose", "grape", "gravel", "grove", "guitar", "hammer", "harbor", "hazel", "heron",
    "hill", "honey", "island", "ivory", "jacket", "jade", "jelly", "jungle", "kayak", "kettle", "kitten",
    "ladder", "lagoon", "lake", "laurel", "lemon", "lilac", "linen", "lizard", "lotus", "lunar", "magnet",
    "maple", "marble", "meadow", "melon", "mint", "mirror", "moss", "motor", "muffin", "nectar", "needle",
    "nickel", "noodle", "north", "oasis", "ocean", "olive", "onion", "orange", "orchid", "otter", "oyster",
    "paddle", "palace", "panda", "paper", "parrot", "peach", "pearl", "pebble", "pepper", "piano", "pillow",
    "pine", "planet", "plum", "pocket", "pond", "poppy", "puzzle", "quartz", "rabbit", "radish", "raven",
    "reef", "ribbon", "river", "robin", "rocket", "rose", "saddle", "sailor", "salmon", "sand", "shadow",
    "shell", "silver", "sketch", "slate", "snow", "spark", "spice", "spider", "spring", "sprout", "stable",
    "star", "stone", "storm", "stream", "summer", "sunset", "swan", "table", "tiger", "timber", "tomato",
    "tower", "trail", "tulip", "tunnel", "turtle", "valley", "velvet", "violet", "walnut", "water", "willow",
    "window", "winter", "wolf", "yellow", "zebra", "anvil", "arch", "atom", "aurora", "axle", "badger",
    "barrel", "basin", "beetle", "bell", "birdie", "boot", "bowl", "brush", "buckle", "bumble", "cake",
    "calm", "canal", "candy", "cape", "cargo", "cart", "cave", "chalk", "charm", "cheese", "chest", "chime",
    "cider", "cinder", "clam", "clove", "coast", "coin", "cookie", "cork", "cove", "crab", "crane", "crayon",
    "crisp", "crumb", "cube", "curl", "dagger", "dandy", "dart", "deer", "dime", "dish", "dock", "doll",
    "dome", "donut", "door", "dough", "dove", "drawer", "dune", "dust", "easel", "fable", "farm", "fawn",
    "fish", "flag", "flax", "fleet", "flute", "foam", "fork", "frame", "frog", "fudge", "gadget", "gale",
    "gate", "gift", "glade", "glass", "glove", "goat", "gold", "gown", "grain", "gull", "hall", "hare",
    "harp", "hawk", "hedge", "helm", "herb", "hive", "hoop", "hornet", "hull", "igloo", "iris", "iron",
    "jazz", "jewel", "kelp", "kite", "knot", "koala", "lace", "lamb", "lamp", "lark", "lava", "leaf", "lens",
    "lily", "lime", "lion", "loaf", "lodge", "loom", "lynx", "mango", "mantle", "mask", "mast", "meal",
    "mist", "mole", "moon", "moth", "mule", "nest", "opal", "orbit", "paint", "palm", "pansy", "pawn",
    "peak", "pear", "pier", "pigeon", "pilot", "pizza", "plank", "plate", "plow", "plume", "polar", "pony",
    "porch", "prism", "pulse", "quail", "quill", "quilt", "radar", "raft", "rain", "ranch", "reed", "ridge",
    "ring", "road", "roof", "root", "rope", "rust", "sage", "sail", "salad", "salt", "satin", "scale",
    "scoop", "seal", "seed", "shark", "sheep", "shelf", "shield", "shoe", "shore", "shrub", "sieve", "silk",
    "sled", "slope", "smoke", "soap", "sock", "soil", "spade", "sponge", "spoon", "spruce", "stamp", "steam",
    "stem", "stool", "straw", "sugar", "swing", "tart", "tent", "thorn", "thread", "tide", "tile", "toast",
    "tool", "torch", "trout", "truck", "trunk", "tube", "tusk", "twig", "umber", "vase", "vine", "violin",
    "wagon", "wasp", "wave", "wheat", "wheel", "whale", "wick", "wing", "wire", "wood", "wool", "yarn",
    "yacht", "yoga", "zinc", "zest", "zone", "alpaca", "amulet", "anthem", "arbor", "banner", "barge",
    "basalt", "bayou", "beech", "bevel", "biome", "bison", "blaze", "bluff", "bobcat", "bolt", "bonsai",
    "boxer", "braid", "brine", "brisk", "bronco", "bugle", "bundt", "bunny", "cairn", "calico", "cameo",
    "canopy", "caper", "cashew", "chalet", "chapel", "chilly", "chorus", "cleat", "cobweb", "condor",
    "cosmos", "coyote", "cozy", "crest", "crow", "cuckoo", "cumin", "dahlia", "dapple", "decoy", "denim",
    "dingo", "dipper", "dizzy", "dory", "drake", "dusky", "eider", "elfin", "ermine", "falls", "ferret",
    "fiesta", "fillet", "fleece", "flurry", "foal", "forge", "fresco", "frond", "galaxy", "gannet", "gecko",
    "geyser", "gizmo", "glow", "gnome", "gopher", "gourd", "gust", "haiku", "hatch", "hearth", "hobby",
    "hollow", "hoopoe", "hummus", "husky", "iguana", "indigo", "inlet", "jackal", "jaguar", "jetty",
    "jigsaw", "jolly", "jumper", "kernel", "kiwi", "lambda", "larch", "legend", "lemur", "lichen", "limpet",
    "linden", "llama", "lupine", "magpie", "mantis", "marlin", "marmot", "mascot", "merlin", "mimosa",
    "mink", "mocha", "mosaic", "mullet", "myrtle", "nimbus", "nutmeg", "ocelot", "oxbow", "peony", "petal",
    "pickle", "plover", "pollen", "pomelo", "potato", "puffin", "pylon", "quince", "quokka", "raisin",
    "rapid", "rattan", "relic", "rhino", "riddle", "ripple", "rowan", "saber", "shrimp", "sierra", "skiff",
    "sloth", "snipe", "sorrel", "sprite", "stoat", "sundae", "tapir", "teapot", "tinsel", "toucan", "tundra",
    "turnip", "umbra", "urchin", "vapor", "vessel", "viper", "vortex", "walrus", "weasel", "wigeon",
    "wombat", "wren", "yonder", "zephyr", "abbey", "acre", "agate", "aloe", "amaze", "angel", "ankle",
    "aroma", "ashen", "aster", "axis", "bacon", "baton", "beam", "bean", "bear", "beet", "bird", "blade",
    "blimp", "bliss", "blue", "boar", "bone", "bonny", "book", "booth", "brave", "brew", "briar", "bright",
    "brown", "buck", "bulb", "bunk", "camp", "cane", "card", "cask", "chain", "chair", "chip", "choir",
    "cinema", "clasp", "climb", "cloak", "cloth", "clown", "coach", "cord", "core", "corn", "couch", "curve",
    "dale", "deck", "dell", "dice", "diner", "ditch", "draft", "dream", "dress", "drill", "drop", "dusk",
    "dwarf", "ebony", "edge", "elder", "epoch", "fairy", "fancy", "feast", "fence", "fiber", "film", "fire",
    "fist", "flask", "flora", "flock", "floor", "flour", "foil", "folk", "font", "ford", "fort", "fresh",
    "fruit", "gaze", "ghost", "giant", "glen", "glide", "goblet", "grace", "grand", "grass", "green",
    "grill", "guest", "guide", "gulf", "habit", "hail", "hand", "happy", "haven", "heart", "heath", "heel",
    "hero", "hinge", "home", "hood", "hope", "horn", "hotel", "humor", "icing", "idea", "inch", "judge",
    "juice", "jump", "keen", "keep", "kind", "king", "knee", "lapel", "latch", "lawn", "leap", "level",
    "light", "link", "list", "lively", "lucky", "lunch", "magic", "major", "maker", "march", "match",
    "mercy", "merry", "metal", "might", "mild", "mill", "mimic", "minor", "model", "mound", "mouse", "movie",
    "music", "noble", "noon", "nurse", "onyx", "opera", "outer", "oval", "ozone", "panel", "patch", "peace",
    "phase", "photo", "plain", "plaza", "plush", "poem", "point", "polo", "pouch", "pride", "prize", "proud",
    "pupil", "quest", "quick", "quiet", "quote", "razor", "ready", "realm", "relay", "rhyme", "rider",
    "rival", "robot", "rocky", "rover", "royal", "ruby", "rural", "sauce", "scarf", "scene", "scout",
    "shade", "shape", "sharp", "sheet", "shine", "shiny", "silly", "sixty", "skate", "skill", "sleek",
    "smile", "smart", "snack", "solar", "solid", "sound", "space", "spare", "speed", "spell", "sport",
    "staff", "stage", "stair", "stand", "stark", "steel", "stern", "stick", "sting", "stock", "story",
    "stove", "sunny", "super", "sweet", "swift", "sword", "tango", "taste", "teach", "thumb", "tidal",
    "token", "topaz", "touch", "towel", "trace", "track", "trade", "trend", "trick", "trust", "twist",
    "ultra", "uncle", "union", "upper", "urban", "usher", "vague", "valid", "value", "vault", "venom",
    "verse", "video", "vigor", "vital", "vivid", "vocal", "voice", "waltz", "watch", "weave", "whisk",
    "wide", "wild", "windy", "witty", "woven", "yield", "young", "youth", "zesty", "anise", "aspic",
    "barrow", "batik", "bloke", "bowler", "brace", "brandy", "bureau", "cadet", "candid", "carol", "casual",
    "cavern", "census", "cheer", "cocoon", "comfy", "cosmic", "cruise", "dainty", "dazzle", "diary",
    "dimple", "dinghy", "dolly", "dowel", "dreamy", "eager", "earthy", "easy", "elite", "enjoy", "equal",
    "fairly", "famous", "finest", "fluffy", "focus", "frosty", "funny", "gentle", "giddy", "glossy", "gleam",
    "grin", "groove", "hearty", "hello", "honest", "humble", "hurry", "jaunty", "jersey", "jiffy", "jingle",
    "kindly", "loyal", "lucid", "lyric", "mellow", "minty", "mighty", "mingle", "moody", "nifty", "nimble",
    "novel", "nutty", "oaken", "oddly", "olden", "peppy", "pixel", "plucky", "polite",
};

const char *setup_passphrase_word(size_t index)
{
    return index < SETUP_PASSPHRASE_WORD_COUNT ? s_words[index] : "";
}

size_t setup_passphrase_format(char *out, size_t out_len, uint32_t r1, uint32_t r2, uint32_t r3)
{
    if (out == NULL || out_len == 0U) {
        return 0U;
    }
    out[0] = '\0';
    const char *first = s_words[r1 % SETUP_PASSPHRASE_WORD_COUNT];
    const char *second = s_words[r2 % SETUP_PASSPHRASE_WORD_COUNT];
    int n = snprintf(out, out_len, "%s-%s-%02u", first, second, (unsigned)(r3 % 100U));
    if (n < 0 || (size_t)n >= out_len) {
        out[0] = '\0';
        return 0U;
    }
    return (size_t)n;
}
