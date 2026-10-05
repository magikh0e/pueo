#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * Spotter signatures — what the detector looks for.
 *
 * Kept apart from the detector itself because this is the part that goes
 * stale. Vendors change contract manufacturers, buy new OUI blocks and
 * revise firmware; the matching logic does not. Editing this file should
 * never require touching Spotter.cpp.
 *
 * Everything here is passive. These are things a device broadcasts on its
 * own: WiFi probe requests it sends looking for a home network, beacons, and
 * BLE advertisements. Nothing in Spotter transmits.
 *
 * Sources, so a future reader can re-check rather than trust:
 *   B4:1E:52     Flock Safety's own IEEE MA-L assignment
 *   00:25:DF     Axon Enterprise's own IEEE MA-L assignment
 *   00:03:7F     Qualcomm Atheros (QCA9377), the radio in several units
 *   0x0D53       Luxottica, BLE company ID in Meta Ray-Ban advertisements
 *   0xFD5F       Meta, BLE service UUID in the same advertisements
 *   0x09C8       XUNTONG, company ID in Penguin battery advertisements
 *
 * The bulk OUI list below is the union of two community collections:
 *   github.com/colonelpanichacks/flock-you      (32 prefixes)
 *   github.com/simeononsecurity/flock-finder    (31 prefixes)
 * 30 of those are common to both; the union is 33. Every one was resolved
 * against the IEEE MA-L registry (standards-oui.ieee.org/oui/oui.csv) before
 * being written down here, and each carries the assignee the registry
 * actually names rather than the word "Flock".
 *
 * That check is the reason they are nearly all Weak. Twenty-three of the 33
 * belong to Liteon and two to Espressif -- contract manufacturers and a
 * module vendor whose blocks are in an enormous amount of unrelated consumer
 * hardware. Reported as a camera on their own they would be wrong far more
 * often than right. What they are good for is corroboration: a Weak OUI on
 * the same MAC as a "Flock-" SSID or a Penguin advertisement is what turns a
 * guess into a finding.
 *
 * Two entries are worth knowing about before trusting them. B8:35:32 is not
 * registered to anyone. 82:6B:F2 has the locally-administered bit set, so it
 * is a randomised address rather than a vendor block -- flock-finder reports
 * newer units using locally-administered MACs specifically to defeat OUI
 * matching, which is also why a recurring one is interesting enough to keep.
 *
 * A word on confidence. An OUI alone is weak evidence: blocks get resold and
 * contract manufacturers build for everyone, so a Qualcomm OUI means "this
 * might be the right radio", not "this is a camera". Two independent fields
 * agreeing is strong. The detector scores accordingly and the UI shows it,
 * because a detector that cries wolf is one you stop believing.
 *
 * Strong carries a second consequence, so mark an entry Strong deliberately.
 * The whole OUI table is matched against WiFi source addresses; only the
 * Strong entries are also matched against BLE addresses, and then only when
 * the address is public. The reason for the split is that an OUI is a claim
 * about who made the radio, and on BLE that claim is worth something only
 * from a block the vendor itself holds. Espressif's block against every
 * public BLE address in range would report a room full of dev boards, and
 * this device is one of them. The reason for the public-address gate is that
 * a random address's top two bits encode the address type rather than a
 * vendor -- and 00:25:DF has top two bits 00, which is the shape of a
 * non-resolvable private address, so the strongest entry in the table is
 * also the one a randomised address could wear by chance.
 *
 * 128-bit services live in kBle128Sigs below. That table exists because
 * this comment used to say they were not expressible and were left out
 * rather than fudged, which was true of a uint16_t service field and stopped
 * being true when Ble128Sig was added. The Flock accessory GATT service it
 * named is in there now.
 *
 * Still not expressible, and still left out rather than fudged: Bluetooth
 * Classic names, which Spotter does not scan for at all. Picking that up
 * means changing the scanner, not this table.
 * ──────────────────────────────────────────────────────────────────────────── */

#include <stddef.h>   // size_t, for the table-length constants below
#include <stdint.h>

namespace Spotter {

enum class Kind : uint8_t { Unknown = 0, Alpr, Glasses, Bodycam, Accessory,
                            Vehicle, Camera, Pentest, Tracker, Mesh };

/* How much a single match is worth. Corroboration -- a second, differently
 * labelled signature on the same MAC -- promotes Likely to Strong. It does
 * not promote Weak: two contract-manufacturer hints agreeing are still two
 * hints. See record() in Spotter.cpp. */
enum class Conf : uint8_t { Weak = 0, Likely, Strong };

struct OuiSig {
  uint8_t oui[3];
  Kind kind;
  Conf conf;
  const char* label;
};

struct NameSig {
  const char* prefix;   // matched case-insensitively against SSID or BLE name
  uint8_t exactLen;     // 0 = any length, else the name must be exactly this long
  Kind kind;
  Conf conf;
  const char* label;
};

struct BleSig {
  uint16_t company;     // BLE manufacturer company ID, 0 = don't care
  uint16_t service;     // 16-bit service UUID, 0 = don't care
  Kind kind;
  Conf conf;
  const char* label;
};

/* 128-bit service UUIDs, which BleSig cannot hold.
 *
 * Bytes are little-endian, the order NimBLE stores them in, so they read
 * backwards against the printed form. The comment on each entry carries the
 * printed UUID; compare against that rather than the array.
 *
 * A vendor-assigned 128-bit service is a much better signature than a 16-bit
 * one: the 16-bit space is allocated by the SIG and shared, while a 128-bit
 * UUID is one somebody generated for their own protocol. That is why these
 * are Strong where an equivalent 16-bit match would not be. */
struct Ble128Sig {
  uint8_t uuid[16];
  Kind kind;
  Conf conf;
  const char* label;
};

/* ── Manufacturer data, past the company ID ─────────────────────────────
 *
 * BleSig can say "company 0x004C", which is every Apple device in range.
 * What distinguishes an AirTag is the byte after the company ID: Find My
 * advertisements carry type 0x12, and nothing in the five tables above
 * could look at it.
 *
 * So this is the company ID plus a prefix of the bytes that follow it.
 * Four bytes is the widest any rule here needs and keeps the row small;
 * prefixLen says how many of them count, so a one-byte type and a
 * three-byte header are the same mechanism.
 *
 * Offsets are from the start of the manufacturer data INCLUDING the two
 * company bytes, which is how NimBLE hands it over. prefix[0] is therefore
 * md[2]. Getting that wrong matches a company ID against a payload byte,
 * which is the kind of mistake that produces a plausible-looking hit. */
struct MfgSig {
  uint16_t company;
  uint8_t  prefix[4];
  uint8_t  prefixLen;      // 1..4, how much of prefix[] is significant
  Kind kind;
  Conf conf;
  const char* label;
};

/* ── Service data, past the UUID ────────────────────────────────────────
 *
 * The same shape for the other place a vendor hides a type byte. This is
 * what separates a Find My Device tag from a shop's beacon: both advertise
 * service 0xFEAA, and the first byte of the service data says which.
 *
 * Pueo had 0xFEAA as "Eddystone beacon, Weak" and could not do better,
 * because seeing the service is all it could see. With the prefix, 0x40 and
 * 0x41 are Google's Find My Device network and get to be Strong, while
 * everything else on 0xFEAA stays the weak beacon guess it always was. */
struct SvcDataSig {
  uint16_t service;        // the 16-bit service UUID carrying the data
  uint8_t  prefix[2];
  uint8_t  prefixLen;      // 1..2
  Kind kind;
  Conf conf;
  const char* label;
};

/* ── Names matched anywhere, not just at the start ──────────────────────
 *
 * NameSig matches a prefix. That is the right default and most of the
 * table depends on it: a prefix plus an exact length is what makes "DR "
 * usable without claiming every name beginning with those characters.
 *
 * It cannot see a vendor in the middle of a name, and the names that matter
 * most here are exactly that shape. A Pineapple's management SSID is
 * "Pineapple_XXXX" but people rename the front of it; a Marauder shows up
 * as "MarauderAP" or with a user string in front.
 *
 * A separate table rather than a mode flag on NameSig, for two reasons.
 * Adding a field would mean rewriting every existing row, and more
 * importantly a substring match is a different risk: it has no anchor, so a
 * short needle hits far more than a short prefix does. Keeping them apart
 * means this table can carry the rule that enforces that, and the comment
 * explaining why, where neither would fit on the shared struct. */
/* ── A whole address, where the whole address is the signature ───────────
 *
 * OuiSig matches three bytes, which is what an OUI is. Some things do not
 * have an OUI: they have one hardcoded address, chosen as a joke, and the
 * first three bytes of it are not a manufacturer claim at all.
 *
 * Matching half of such an address is a weaker signature than the thing it
 * stands for, which is why the Pwnagotchi row lived at Conf::Likely with a
 * comment apologising for it. With six bytes it can be Strong and mean it.
 *
 * WiFi only, deliberately. Every address this table can hold is locally
 * administered -- that is what makes it a made-up address -- and the BLE
 * cross-match only fires on public addresses, so a BLE pass would be dead
 * code dressed as thoroughness. */
struct MacSig {
  uint8_t mac[6];
  Kind kind;
  Conf conf;
  const char* label;
};

struct NameInSig {
  const char* needle;      // found anywhere in the name, case-insensitively
  uint8_t minNameLen;      // 0 = any; else the name must be at least this long
  Kind kind;
  Conf conf;
  const char* label;
};

/* ── What belongs on this screen ─────────────────────────────────────────
 *
 * Hardware whose purpose is observing other people, and which announces
 * itself without being asked. A handheld action camera is a camera and is
 * not surveillance, so none are here. The line is purpose, not optics.
 * ───────────────────────────────────────────────────────────────────────── */

/* ── Fixed full addresses ───────────────────────────────────────────────── */
static const MacSig kMacSigs[] = {
  /* de:ad:be:ef:de:ad, hardcoded in Pwnagotchi and the same on every unit.
   *
   * Strong, and it earns it here in a way the three-byte version could not:
   * this is the entire address, not a prefix somebody else's test rig might
   * share. The advertisement also carries JSON with name, version, pwnd_tot,
   * policy.deauth and uptime; none of it is read here, because this table
   * matches and does not parse. */
  {{0xDE, 0xAD, 0xBE, 0xEF, 0xDE, 0xAD}, Kind::Pentest, Conf::Strong,
   "Pwnagotchi"},
};

/* ── WiFi: source MAC prefixes ─────────────────────────────────────── */
/* The scan breaks on the first match, so anything that should outrank a
 * generic block has to sit above it. */
static const OuiSig kOuiSigs[] = {
  /* ── Fleet telematics and RV kit ─────────────────────────────────────── */
  {{0x28, 0xEA, 0x5B}, Kind::Vehicle, Conf::Strong, "Samsara fleet"},
  {{0xFC, 0xDB, 0x21}, Kind::Vehicle, Conf::Strong, "Samsara fleet"},
  {{0x98, 0x5D, 0x46}, Kind::Vehicle, Conf::Strong, "PeopleNet ELD"},
  {{0x00, 0x17, 0x1A}, Kind::Vehicle, Conf::Strong, "Winegard RV"},

  /* Aftermarket CarPlay dongles. Weak: the same boards are sold under many
   * names, which is the Liteon problem in a different market. */
  {{0xCC, 0x57, 0x63}, Kind::Vehicle, Conf::Weak,   "CarPlay dongle?"},
  {{0x68, 0x8F, 0xC9}, Kind::Vehicle, Conf::Weak,   "CarPlay dongle?"},

  /* ── Roadside and municipal surveillance ─────────────────────────────── */
  /* Vendor-own IEEE blocks, so Strong, which also allows them on the BLE
   * path. See the note at the head of this table. */
  {{0x00, 0x0E, 0xA5}, Kind::Alpr, Conf::Strong,  "BLIP Systems"},
  {{0x00, 0x14, 0x7B}, Kind::Alpr, Conf::Strong,  "Iteris (BlueTOAD)"},
  {{0xD4, 0x11, 0xD6}, Kind::Alpr, Conf::Strong,  "ShotSpotter"},

  /* Uniview, five blocks. Commercial CCTV that also turns up on poles. */
  {{0x14, 0xBA, 0x88}, Kind::Camera, Conf::Strong, "Uniview"},
  {{0x48, 0xEA, 0x63}, Kind::Camera, Conf::Strong, "Uniview"},
  {{0x6C, 0xF1, 0x7E}, Kind::Camera, Conf::Strong, "Uniview"},
  {{0x88, 0x26, 0x3F}, Kind::Camera, Conf::Strong, "Uniview"},
  {{0xC4, 0x79, 0x05}, Kind::Camera, Conf::Strong, "Uniview"},

  /* ── Body-worn and in-car video ──────────────────────────────────────── */
  {{0x00, 0x23, 0xBD}, Kind::Bodycam, Conf::Strong, "Digital Ally"},

  /* ── The boxes that sit in the car with them ─────────────────────────── */
  /* Not cameras, so Kind::Accessory rather than a camera kind: a fleet
   * router is the support gear a surveillance vehicle carries, which is the
   * same thing Flock's accessory service and the pole batteries are. */
  {{0x00, 0x30, 0x44}, Kind::Accessory, Conf::Strong, "Cradlepoint"},
  {{0x00, 0xE0, 0x1C}, Kind::Accessory, Conf::Strong, "Cradlepoint"},
  {{0x00, 0x14, 0x3E}, Kind::Accessory, Conf::Strong, "Sierra AirLink"},
  {{0x00, 0xA0, 0xD5}, Kind::Accessory, Conf::Strong, "Sierra AirLink"},
  {{0x28, 0xA3, 0x31}, Kind::Accessory, Conf::Strong, "Sierra AirLink"},
  {{0x50, 0x13, 0x9D}, Kind::Accessory, Conf::Strong, "Sierra AirLink"},
  {{0x64, 0xCE, 0x6E}, Kind::Accessory, Conf::Strong, "Sierra AirLink"},
  {{0x84, 0xDB, 0x2F}, Kind::Accessory, Conf::Strong, "Sierra AirLink"},
  {{0xCC, 0x93, 0x4A}, Kind::Accessory, Conf::Strong, "Sierra AirLink"},
  {{0x00, 0x09, 0xBC}, Kind::Accessory, Conf::Strong, "Utility Inc"},
  {{0x00, 0x16, 0xED}, Kind::Accessory, Conf::Strong, "Utility Inc"},

  /* Weak, and for the reason the Liteon blocks above are weak: the hardware
   * is not exclusive to the use. Novatel's prefix is also on consumer MiFi
   * hotspots, and Compex sells the same boards to everyone. */
  {{0x28, 0x80, 0xA2}, Kind::Accessory, Conf::Weak,   "Inseego (MiFi?)"},
  {{0x00, 0x40, 0x29}, Kind::Accessory, Conf::Weak,   "Compex (shared board?)"},
  {{0x00, 0x80, 0x48}, Kind::Accessory, Conf::Weak,   "Compex (shared board?)"},
  {{0x04, 0xF0, 0x21}, Kind::Accessory, Conf::Weak,   "Compex (shared board?)"},

  /* Flock-family pole batteries. Weak on the OUI because the name is the
   * stronger signal here, and kNameInSigs is what carries it. */
  {{0x04, 0x0D, 0x84}, Kind::Accessory, Conf::Weak,   "FS battery?"},
  {{0x1C, 0x34, 0xF1}, Kind::Accessory, Conf::Weak,   "FS battery?"},
  {{0x38, 0x5B, 0x44}, Kind::Accessory, Conf::Weak,   "FS battery?"},
  {{0x94, 0x34, 0x69}, Kind::Accessory, Conf::Weak,   "FS battery?"},
  {{0xB4, 0xE3, 0xF9}, Kind::Accessory, Conf::Weak,   "FS battery?"},
  {{0xF0, 0x82, 0xC0}, Kind::Accessory, Conf::Weak,   "FS battery?"},

  /* ── Fixed home cameras ──────────────────────────────────────────────── */
  {{0x48, 0x62, 0x64}, Kind::Camera, Conf::Strong, "Arlo"},
  {{0xA4, 0x11, 0x62}, Kind::Camera, Conf::Strong, "Arlo"},
  {{0xFC, 0x9C, 0x98}, Kind::Camera, Conf::Strong, "Arlo"},

  /* Flock Safety's own block. Strong on its own. */
  {{0xB4, 0x1E, 0x52}, Kind::Alpr, Conf::Strong,  "Flock Safety"},

  /* The radio in several units. Qualcomm builds for everyone. */
  {{0x00, 0x03, 0x7F}, Kind::Alpr, Conf::Weak,    "Atheros QCA9377"},

  /* Axon Enterprise's own IEEE block, and the only body-camera signature
   * here. Strong on the vendor, not on the product: Axon also builds the
   * docks, the in-car Fleet systems and the TASERs, so this says Axon
   * hardware is in range rather than specifically a camera on a shoulder.
   * Axon Body units carry WiFi for dock upload and Axon View, which is what
   * makes them audible at all. Unlike the block below it, this one is the
   * vendor's own assignment rather than a contract manufacturer's. */
  {{0x00, 0x25, 0xDF}, Kind::Bodycam, Conf::Strong, "Axon Enterprise"},

  /* Liteon Technology Corporation -- 23 blocks across the two lists. */
  {{0x00, 0xF4, 0x8D}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x14, 0x5A, 0xFC}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x14, 0xB5, 0xCD}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x24, 0xB2, 0xB9}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x3C, 0x91, 0x80}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x58, 0x00, 0xE3}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x5C, 0x93, 0xA2}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x64, 0x6E, 0x69}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x70, 0x08, 0x94}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x70, 0xC9, 0x4E}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x74, 0x4C, 0xA1}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x80, 0x30, 0x49}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x94, 0x08, 0x53}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x9C, 0x2F, 0x9D}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0xB8, 0x1E, 0xA4}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0xC0, 0x35, 0x32}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0xD0, 0x39, 0x57}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0xD8, 0xF3, 0xBC}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0xE0, 0x0A, 0xF6}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0xE4, 0xAA, 0xEA}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0xE8, 0xD0, 0xFC}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0xF4, 0x6A, 0xDD}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0xF8, 0xA2, 0xD6}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},

  /* Silicon Laboratories -- 3 blocks across the two lists. */
  {{0x58, 0x8E, 0x81}, Kind::Alpr, Conf::Weak,    "SiLabs (ALPR?)"},
  {{0x90, 0x35, 0xEA}, Kind::Alpr, Conf::Weak,    "SiLabs (ALPR?)"},
  {{0xEC, 0x1B, 0xBD}, Kind::Alpr, Conf::Weak,    "SiLabs (ALPR?)"},

  /* Espressif Inc -- 2 blocks across the two lists. */
  {{0x3C, 0x71, 0xBF}, Kind::Alpr, Conf::Weak,    "Espressif (ALPR?)"},
  {{0xA4, 0xCF, 0x12}, Kind::Alpr, Conf::Weak,    "Espressif (ALPR?)"},

  /* Universal Global Scientific Industrial (USI) -- 2 blocks. */
  {{0x08, 0x3A, 0x88}, Kind::Alpr, Conf::Weak,    "USI (ALPR?)"},
  {{0xE0, 0x4F, 0x43}, Kind::Alpr, Conf::Weak,    "USI (ALPR?)"},

  /* Samsung Electronics -- 1 block. */
  {{0x48, 0x27, 0xEA}, Kind::Alpr, Conf::Weak,    "Samsung (ALPR?)"},

  /* Neither of these is a vendor block. B8:35:32 is in no IEEE registry;
   * 82:6B:F2 has the locally-administered bit set, which makes it a
   * randomised address that happens to keep recurring. */
  {{0x82, 0x6B, 0xF2}, Kind::Alpr, Conf::Weak,    "LAA, not a vendor"},
  /* Vendors whose product lines include plate readers, from SquachWatch-CYD
   * (GPL-3.0) and checked here against the IEEE registry rather than taken on
   * its word: all seven are MA-L blocks assigned to the named company.
   *
   * Likely rather than Strong, which is a narrower call than the one Axon
   * gets. Axon is a small company -- tasers, body cameras, fleet, evidence --
   * so its block means Axon hardware and that is nearly always interesting.
   * Motorola Solutions is the public-safety radio industry, plus Avigilon,
   * plus Vigilant's LPR line; one of its addresses is far more likely a
   * handheld radio than a camera. Genetec is narrower but still sells VMS and
   * access control alongside AutoVu.
   *
   * Strong is also what reaches the BLE path, so promoting these would widen
   * BLE matching on the strength of a guess about which product it is.
   * Corroboration still gets them there: a Motorola block plus an ALPR
   * network name on the same address promotes to Strong on its own. */
  {{0x00, 0x04, 0x7D}, Kind::Alpr, Conf::Likely,  "Motorola Solutions"},
  {{0x00, 0x18, 0x85}, Kind::Alpr, Conf::Likely,  "Motorola Solutions"},
  {{0x00, 0x1F, 0x92}, Kind::Alpr, Conf::Likely,  "Motorola Solutions"},
  {{0x4C, 0xCC, 0x34}, Kind::Alpr, Conf::Likely,  "Motorola Solutions"},
  {{0xB8, 0xE2, 0x8C}, Kind::Alpr, Conf::Likely,  "Motorola Malaysia"},
  {{0x00, 0xBF, 0x15}, Kind::Alpr, Conf::Likely,  "Genetec"},
  {{0x0C, 0xBF, 0x15}, Kind::Alpr, Conf::Likely,  "Genetec"},

  /* ── Fixed cameras and doorbells ──────────────────────────────────────
   * From SquachWatch-CYD (GPL-3.0), every block re-checked against the IEEE
   * registry here rather than taken on its word.
   *
   * Strong, on the same rule Axon gets: the block belongs to the vendor, and
   * the label names the vendor rather than the product. These six are camera
   * companies -- that is what they sell -- so the vendor claim and the
   * product claim nearly coincide, which is not true of Motorola above.
   *
   * Understand what this does to BLE. Strong is the grade that reaches the
   * BLE path, so this takes that set from two blocks to twenty-four. Ring
   * doorbells are on ordinary streets in numbers that Flock cameras are not,
   * and Spotter will say so. Those are true positives, but the screen goes
   * from quiet to busy in a residential area, and that is a change in what
   * the feature is for rather than a bug in it. */
  {{0xAC, 0x9F, 0xC3}, Kind::Camera, Conf::Strong, "Ring"},
  {{0x18, 0x7F, 0x88}, Kind::Camera, Conf::Strong, "Ring"},
  {{0x34, 0x3E, 0xA4}, Kind::Camera, Conf::Strong, "Ring"},
  {{0x54, 0xE0, 0x19}, Kind::Camera, Conf::Strong, "Ring"},
  {{0x5C, 0x47, 0x5E}, Kind::Camera, Conf::Strong, "Ring"},
  {{0x64, 0x9A, 0x63}, Kind::Camera, Conf::Strong, "Ring"},
  {{0x90, 0x48, 0x6C}, Kind::Camera, Conf::Strong, "Ring"},
  {{0x9C, 0x76, 0x13}, Kind::Camera, Conf::Strong, "Ring"},
  {{0xCC, 0x3B, 0xFB}, Kind::Camera, Conf::Strong, "Ring"},
  {{0xC4, 0xDB, 0xAD}, Kind::Camera, Conf::Strong, "Ring"},
  {{0x24, 0x2B, 0xD6}, Kind::Camera, Conf::Strong, "Ring"},
  {{0x00, 0xB4, 0x63}, Kind::Camera, Conf::Strong, "Ring"},
  {{0x50, 0xE4, 0x67}, Kind::Camera, Conf::Strong, "Ring"},
  {{0xC0, 0x56, 0xE3}, Kind::Camera, Conf::Strong, "Hikvision"},
  {{0x44, 0x19, 0xB6}, Kind::Camera, Conf::Strong, "Hikvision"},
  {{0x28, 0x57, 0xBE}, Kind::Camera, Conf::Strong, "Hikvision"},
  {{0x2C, 0xAA, 0x8E}, Kind::Camera, Conf::Strong, "Wyze Labs"},
  {{0xD0, 0x3F, 0x27}, Kind::Camera, Conf::Strong, "Wyze Labs"},
  {{0x7C, 0x78, 0xB2}, Kind::Camera, Conf::Strong, "Wyze Labs"},
  {{0x00, 0x40, 0x8C}, Kind::Camera, Conf::Strong, "Axis Comms"},
  {{0xB8, 0xA4, 0x4F}, Kind::Camera, Conf::Strong, "Axis Comms"},
  {{0xE0, 0xA7, 0x00}, Kind::Camera, Conf::Strong, "Verkada"},
  {{0x70, 0x1A, 0xD5}, Kind::Camera, Conf::Strong, "Avigilon Alta"},

  /* Amazon owns Ring, and also Echo, Fire TV, Kindle and eero. A block of
   * theirs is a coin flip at best, so it stays off the BLE path. */
  {{0x34, 0xD2, 0x70}, Kind::Camera, Conf::Weak,   "Amazon (Ring parent)"},
  {{0xF0, 0x27, 0x2D}, Kind::Camera, Conf::Weak,   "Amazon (Ring parent)"},
  {{0xFC, 0x65, 0xDE}, Kind::Camera, Conf::Weak,   "Amazon (Ring parent)"},
  {{0x68, 0x37, 0xE9}, Kind::Camera, Conf::Weak,   "Amazon (Ring parent)"},

  /* Module vendors found inside cameras. Same class as the Liteon rows. */
  {{0xB8, 0xD7, 0xAF}, Kind::Camera, Conf::Weak,   "Murata (module)"},
  {{0x00, 0xE0, 0x4C}, Kind::Camera, Conf::Weak,   "Realtek (module)"},
  {{0xA4, 0xC1, 0x38}, Kind::Camera, Conf::Weak,   "Telink (module)"},
  {{0x4C, 0x69, 0x05}, Kind::Camera, Conf::Weak,   "unregistered OUI"},

  /* ── Pentest hardware ─────────────────────────────────────────────────
   * Not surveillance, but the same question: what is in range that someone
   * brought deliberately.
   *
   * Flipper's block is their own MA-L assignment, so it grades like the
   * others. The two Hak5 entries are locally-administered addresses -- the
   * 0x02 bit in the first octet -- which is to say defaults a Pineapple ships
   * with rather than anything IEEE assigned. Anyone can set them, so they are
   * Weak and labelled LAA, which is also what keeps them off the BLE path. */
  {{0x0C, 0xFA, 0x22}, Kind::Pentest, Conf::Strong, "Flipper Devices"},
  {{0x02, 0xC0, 0xCA}, Kind::Pentest, Conf::Weak,   "Hak5 LAA default"},
  {{0x02, 0x13, 0x37}, Kind::Pentest, Conf::Weak,   "Hak5 LAA default"},

  /* ALFA's own block, and the one 02:C0:CA above is derived from: set the
   * locally-administered bit on 00:C0:CA and you get it. A Pineapple wearing
   * the LAA default is advertising a spoof of this.
   *
   * Weak, and it has to stay Weak. ALFA sell adapters to everyone doing
   * wireless work, so this means "an ALFA radio is in range" and not "a
   * Pineapple is". What it is good for is the thing the Liteon entries above
   * are good for: sitting under a stronger signature on the same MAC and
   * turning a guess into a finding.
   *
   * Three other OUIs are commonly listed alongside this one as Hak5 markers
   * and none of them are here. 00:13:EF and A4:2B:B0 get cited as Atheros or
   * Qualcomm and the IEEE registry says Kingjon Digital Technology and
   * TP-LINK; whoever wrote that list did not check. 00:E0:4C really is
   * Realtek, and Realtek is in so much unrelated consumer hardware that even
   * Weak would be generous. */
  {{0x00, 0xC0, 0xCA}, Kind::Pentest, Conf::Weak,   "ALFA (Pineapple radio?)"},

  /* Pwnagotchi beacons from de:ad:be:ef:de:ad, a fixed address in its own
   * source rather than a vendor block. Only the first three bytes are
   * checked here, which is the table's shape, and de:ad:be is a joke prefix
   * with no registered assignee, so the loss of specificity is small.
   *
   * Likely, not Strong, and check_spotter_oui.py is why. It was written as
   * Strong on the argument that the BLE cross-match only fires on public
   * addresses and anything beginning de:ad:be has the local bit set, so that
   * path could never reach it. True about the mechanism and beside the
   * point: Strong in this table means "a vendor's own IEEE block", and a
   * joke prefix with no assignee is not one whatever the code does with it.
   *
   * The grade also matches what is actually being matched. Pwnagotchi's
   * address is de:ad:be:ef:de:ad and this sees three bytes of six, so the
   * signature really is weaker than the thing it stands for. A second hit on
   * the same MAC promotes it.
   *
   * The advertisement also carries JSON with name, version, pwnd_tot,
   * policy.deauth and uptime. None of that is read here: this table matches,
   * it does not parse. Worth knowing it is there if the detail is ever
   * wanted. */
  /* The prefix stays, demoted and renamed. kMacSigs above carries the
   * whole address and names Pwnagotchi; this is what is left over, which is
   * "somebody set a deadbeef MAC". That is a real hint and it is not an
   * identification, and the label now says which it is. A unit running the
   * stock address hits both and comes out corroborated. */
  {{0xDE, 0xAD, 0xBE}, Kind::Pentest, Conf::Weak,   "deadbeef MAC"},

  {{0xB8, 0x35, 0x32}, Kind::Alpr, Conf::Weak,    "unregistered OUI"},
};

/* ── WiFi: SSID patterns in probe requests and beacons ───────────────────── */
/* Ordered most specific first: nameMatch stops at the first hit, so a bare
 * "Flock" above "Flock-" would swallow the provisioning SSIDs and report them
 * at the lower confidence. */
static const NameSig kSsidSigs[] = {
  {"Flock Camera net", 0, Kind::Alpr, Conf::Strong, "Flock camera SSID"},
  {"Flock-",           0, Kind::Alpr, Conf::Strong, "Flock SSID"},

  /* Provisioned units drop the suffix. Still specific, but a bare word is a
   * bare word and somebody's home network can be called this. */
  {"Flock",            0, Kind::Alpr, Conf::Likely, "Flock (bare)"},

  {"Penguin-", 0, Kind::Accessory, Conf::Likely, "Flock battery pack"},

  /* The O.MG cable brings up its own access point for command and control,
   * and the SSID it is flashed with by default is in their own tooling:
   *
   *   O-MG/O.MG-Firmware, flash.py
   *     self.WIFI_SSID = "O.MG"
   *
   * Likely rather than Strong because it is a default and the flasher takes
   * an override, so anyone deploying one deliberately has probably changed
   * it. What it catches is one left as shipped, which is worth catching:
   * the whole point of the device is that it looks like a cable. */
  {"O.MG", 0, Kind::Pentest, Conf::Likely, "O.MG cable (default SSID)"},
};

/* ── BLE: advertisement contents ─────────────────────────────────────────── */
static const BleSig kBleSigs[] = {
  /* ── Fleet telematics ────────────────────────────────────────────────── */
  /* A camera-and-tracking box in a commercial truck. Surveillance gear that
   * happens to be bolted to a vehicle. */
  {0x0B6B, 0x0000, Kind::Vehicle, Conf::Strong, "Samsara fleet"},

  /* ── Tyre sensors ────────────────────────────────────────────────────────
   *
   * Worth more than they look. A TPMS broadcasts a stable identifier for one
   * wheel, continuously, and the owner did not choose it and cannot switch
   * it off. Following a car by its tyres is a known technique and this is
   * the table that can say so.
   *
   * Strong on the vendor-own company IDs, because nothing else uses them.
   * These are the BLE sensors; the 315 and 433 MHz valve-stem kind are the
   * CC1101's problem, not this one. */
  {0x0601, 0x0000, Kind::Vehicle, Conf::Strong, "Schrader TPMS"},
  {0x0B99, 0x0000, Kind::Vehicle, Conf::Strong, "Goodyear tyre"},
  {0x0E32, 0x0000, Kind::Vehicle, Conf::Strong, "Pacific TPMS"},
  {0x070A, 0x0000, Kind::Vehicle, Conf::Strong, "Huf tyre/PEPS"},
  {0x0127, 0x0000, Kind::Vehicle, Conf::Strong, "FOBO TPMS"},
  {0x0BA2, 0x0000, Kind::Vehicle, Conf::Strong, "TireCheck TPMS"},
  {0x0000, 0x00EE, Kind::Vehicle, Conf::Likely, "FOBO TPMS"},
  {0x0000, 0x27A5, Kind::Vehicle, Conf::Likely, "SYTPMS sensor"},
  {0x0000, 0xFBB0, Kind::Vehicle, Conf::Likely, "Aftermarket TPMS"},
  /* The SIG's own Tyre Pressure Monitoring service. Allocated rather than
   * vendor-assigned, so anything implementing the standard lands here and
   * the grade says as much. */
  {0x0000, 0x1860, Kind::Vehicle, Conf::Weak,   "TPMS service"},

  /* ── The car population ──────────────────────────────────────────────────
   *
   * Everything from here to the end of this block is a car rather than
   * something watching you: phone-as-key, factory hotspots, infotainment.
   * Weak throughout, which is the grade that reads as corroboration rather
   * than as a finding, because a car park full of Strong hits would drown
   * the rows that matter.
   *
   * Kept contiguous on purpose. If this turns out to be noise in traffic it
   * is one block to delete. */
  {0x022B, 0x0000, Kind::Vehicle, Conf::Weak, "Tesla"},
  {0x0000, 0xFE96, Kind::Vehicle, Conf::Weak, "Tesla"},
  {0x0000, 0xFE97, Kind::Vehicle, Conf::Weak, "Tesla"},
  {0x05EB, 0x0000, Kind::Vehicle, Conf::Weak, "BMW"},
  {0x0723, 0x0000, Kind::Vehicle, Conf::Weak, "Ford/Lincoln"},
  {0x0977, 0x0000, Kind::Vehicle, Conf::Weak, "Toyota/Lexus"},
  {0x0915, 0x0000, Kind::Vehicle, Conf::Weak, "Honda/Acura"},
  {0x0826, 0x0000, Kind::Vehicle, Conf::Weak, "Hyundai/Genesis"},
  {0x0BA6, 0x0000, Kind::Vehicle, Conf::Weak, "Nissan/Infiniti"},
  {0x0A10, 0x0000, Kind::Vehicle, Conf::Weak, "Subaru"},
  {0x011F, 0x0000, Kind::Vehicle, Conf::Weak, "Volkswagen"},
  {0x0000, 0xFE30, Kind::Vehicle, Conf::Weak, "Volkswagen"},
  {0x0000, 0xFE31, Kind::Vehicle, Conf::Weak, "Volkswagen"},
  {0x010E, 0x0000, Kind::Vehicle, Conf::Weak, "Audi"},
  {0x0120, 0x0000, Kind::Vehicle, Conf::Weak, "Porsche"},
  {0x017C, 0x0000, Kind::Vehicle, Conf::Weak, "Mercedes"},
  {0x020B, 0x0000, Kind::Vehicle, Conf::Weak, "Jaguar/Land Rover"},
  {0x0068, 0x0000, Kind::Vehicle, Conf::Weak, "GM"},
  {0x0941, 0x0000, Kind::Vehicle, Conf::Weak, "Rivian"},
  {0x0C34, 0x0000, Kind::Vehicle, Conf::Weak, "BYD"},

  /* XUNTONG, the BLE manufacturer ID on Penguin's Flock-family pole
   * battery. Likely on its own; the name beside it makes it Strong. */
  {0x09C8, 0x0000, Kind::Accessory, Conf::Likely, "Penguin battery"},

  /* Company and service together. Two independent fields agreeing is about
   * as good as passive identification gets. */
  {0x0D53, 0xFD5F, Kind::Glasses, Conf::Strong, "Meta Ray-Ban"},

  /* Either field alone: same hardware family, less certainty. */
  {0x0D53, 0x0000, Kind::Glasses, Conf::Likely, "Luxottica eyewear"},
  {0x0000, 0xFD5F, Kind::Glasses, Conf::Likely, "Meta device"},

  /* The Penguin battery pack's advertisements carry XUNTONG's company ID
   * with a serial in the payload. SIG company IDs are assigned, so this is
   * a good deal more specific than an OUI. */
  {0x09C8, 0x0000, Kind::Accessory, Conf::Likely, "XUNTONG (Penguin)"},

  /* ── Item trackers ────────────────────────────────────────────────────
   * 16-bit service UUIDs, checked against the Bluetooth SIG's own assigned
   * numbers rather than against the project they came from.
   *
   * Tile's two are assigned to "Tile, Inc." and Tile sells nothing but
   * trackers, so the member claim and the product claim coincide. Strong.
   *
   * 0xFD5A is assigned to "Samsung Electronics Co., Ltd." -- not to SmartTag.
   * That it is the SmartTag discovery UUID is widely reported and probably
   * right, but it is a product claim the registry does not make, and Samsung
   * ships rather a lot that is not a tracker. Likely, and the label says so.
   *
   * 0xFEAA is assigned to Google LLC and is Eddystone, the general beacon
   * format. Retail and asset beacons use it as readily as Find My Device.
   * Weak, and labelled for what the registry says rather than what it might
   * be doing. */
  {0x0000, 0xFEED, Kind::Tracker, Conf::Strong, "Tile"},
  {0x0000, 0xFEEC, Kind::Tracker, Conf::Strong, "Tile"},
  {0x0000, 0xFD5A, Kind::Tracker, Conf::Likely, "Samsung (SmartTag?)"},
  {0x0000, 0xFEAA, Kind::Tracker, Conf::Weak,   "Eddystone beacon"},

  /* Raven camera GATT. The documented range is 0x3100-0x3500 and the struct
   * matches one value at a time, so these are the three that are actually
   * named as carrying data. They are 16-bit UUIDs outside the SIG-assigned
   * space, where any vendor may collide, hence Weak. */
  /* Flipper Zero's BLE serial profile advertises 0x3080 with the case
   * colour OR'd into the low nibble. From their own firmware,
   * targets/f7/ble_glue/profiles/serial_profile.c:
   *
   *     .Service_UUID_16 = 0x3080,
   *     config->adv_service.Service_UUID_16 |= furi_hal_version_get_hw_color();
   *
   * Four lines rather than a mask, because BleSig matches exact values and
   * adding a mask field for one signature is not worth it. The cost is that
   * a colour Flipper ships later needs a line here; the low nibble is the
   * only thing that varies.
   *
   * Likely rather than Strong. This is the unallocated 16-bit space, which
   * nothing stops another vendor using, and 0C:FA:22 already covers a
   * Flipper whose BLE address is public. What these add is the case where it
   * is not, and corroboration promotes them when both fire. */
  {0x0000, 0x3080, Kind::Pentest, Conf::Likely, "Flipper Zero"},
  {0x0000, 0x3081, Kind::Pentest, Conf::Likely, "Flipper Zero (black)"},
  {0x0000, 0x3082, Kind::Pentest, Conf::Likely, "Flipper Zero (white)"},
  {0x0000, 0x3083, Kind::Pentest, Conf::Likely, "Flipper Zero (clear)"},

  {0x0000, 0x3100, Kind::Alpr, Conf::Weak, "Raven GATT 3100"},
  {0x0000, 0x3101, Kind::Alpr, Conf::Weak, "Raven GATT 3101"},
  {0x0000, 0x3102, Kind::Alpr, Conf::Weak, "Raven GATT 3102"},
};

/* ── BLE: advertised names ───────────────────────────────────────────────── */
static const NameSig kBleNameSigs[] = {
  {"Penguin-",       0, Kind::Accessory, Conf::Likely, "Flock battery pack"},
  {"FS Ext Battery", 0, Kind::Accessory, Conf::Strong, "Flock ext battery"},
  {"Ray-Ban",        0, Kind::Glasses,   Conf::Strong, "Ray-Ban Meta"},
  {"Spectacles",     0, Kind::Glasses,   Conf::Strong, "Snap Spectacles"},

  /* The stock Nordic bootloader name. It is in the community lists because
   * the battery pack advertises it while updating, but so does every other
   * Nordic device in DFU mode, so on its own it means almost nothing. Kept
   * because it costs one row and it corroborates a Penguin sitting next to
   * it. */
  {"DfuTarg",        0, Kind::Accessory, Conf::Weak,   "Nordic DFU (generic)"},

  /* KARR: a dealer-installed BLE immobiliser and remote control, found by
   * UC San Diego's "BLE Theft Auto" (USENIX Security 2026). They wardrove San
   * Diego's highways and KARR came second only to Tesla -- 608 of 4,314 unique
   * devices -- because dealerships fit one to every car on the lot. Two models:
   * QT is BLE-only, DR adds cellular. The paper gives both as a prefix plus
   * exactly eight characters, which is where exactLen earns its place: "dr "
   * matched case-insensitively against any name is a wide net.
   *
   * Likely, not Strong, and labelled as a module rather than as a finding. The
   * paper's vulnerability is a fixed key shared across every unit, patched by
   * the vendor on 2026-07-20 -- but the fix has to be applied by hand and
   * nothing in the advertisement says whether it has been. So this says a KARR
   * module is in range. It does not say the car is open. */
  {"QT ", 11, Kind::Vehicle, Conf::Likely, "KARR BT module"},
  {"DR ", 11, Kind::Vehicle, Conf::Likely, "KARR Cell module"},
};

/* ── BLE: 128-bit service UUIDs ────────────────────────────────────── */
static const Ble128Sig kBle128Sigs[] = {
  /* 6ba1b218-15a8-461f-9fa8-5dcae273eafd
   *
   * Meshtastic's BLE service, from their own firmware and documentation.
   * Not surveillance gear, which is why it is Kind::Mesh: it says somebody
   * is running a LoRa mesh node in range, which is worth knowing and is not
   * the same claim as a camera.
   *
   * Cross-checked against Meshtastic's published UUID rather than taken on
   * trust from where it was found. */
  {{0xFD, 0xEA, 0x73, 0xE2, 0xCA, 0x5D, 0xA8, 0x9F,
    0x1F, 0x46, 0xA8, 0x15, 0x18, 0xB2, 0xA1, 0x6B},
   Kind::Mesh, Conf::Strong, "Meshtastic node"},

  /* e8ccbb38-9532-46a8-9fe5-1814df172e6f
   *
   * The Flock accessory GATT service this file has named as missing since it
   * was written. Now that a 128-bit table exists it goes in. */
  {{0x6F, 0x2E, 0x17, 0xDF, 0x14, 0x18, 0xE5, 0x9F,
    0xA8, 0x46, 0x32, 0x95, 0x38, 0xBB, 0xCC, 0xE8},
   Kind::Accessory, Conf::Strong, "Flock accessory"},
};

/* ── BLE: manufacturer data ──────────────────────────────────────────────
 *
 * Ordered most specific first, like every other table here: the scan stops
 * at the first hit.
 */
static const MfgSig kMfgSigs[] = {
  /* Aftermarket BLE valve-cap TPMS, company 0x0001 with a wheel-position
   * byte. Company 0x0001 is Nordic's, so the prefix is doing all the work
   * and the grade says so. */
  {0x0001, {0x80, 0, 0, 0}, 1, Kind::Vehicle, Conf::Likely, "TPMS (front L)"},
  {0x0001, {0x81, 0, 0, 0}, 1, Kind::Vehicle, Conf::Likely, "TPMS (front R)"},
  {0x0001, {0x82, 0, 0, 0}, 1, Kind::Vehicle, Conf::Likely, "TPMS (rear L)"},
  {0x0001, {0x83, 0, 0, 0}, 1, Kind::Vehicle, Conf::Likely, "TPMS (rear R)"},

  /* Apple Find My, company 0x004C, advertisement type 0x12.
   *
   * This is the rule whose absence meant Surveillance never flagged an
   * AirTag. Likely rather than Strong, and the reason matters: an iPhone
   * relays Find My for other people's accessories, so this says "something
   * here is on the Find My network", not "there is an AirTag here". A phone
   * in a pocket sets it off and that is correct behaviour, not a false
   * positive. Walking it down is Hunt's job.
   *
   * 0x12 is the separated-from-owner payload. An accessory near its owner
   * advertises type 0x07 instead and is not interesting to this screen:
   * somebody's own tag beside them is not a tracker on you. */
  {0x004C, {0x12, 0, 0, 0}, 1, Kind::Tracker, Conf::Likely,
   "Apple Find My (separated)"},

  /* Tile, company 0x00B1 but the useful part is the service, which kBleSigs
   * already has on 0xFEED/0xFEEC. Nothing to add here. */
};

/* ── BLE: service data ───────────────────────────────────────────────────── */
static const SvcDataSig kSvcDataSigs[] = {
  /* Google Find My Device network, service 0xFEAA with frame type 0x40 or
   * 0x41. Strong, unlike the bare 0xFEAA row in kBleSigs, because the frame
   * type is Google's and a retail Eddystone beacon does not use it.
   *
   * This pair is why SvcDataSig exists. Pueo already saw 0xFEAA and could
   * only call it "Eddystone beacon, Weak", so a Chipolo, a Pebblebee or a
   * moto tag read as shop furniture. */
  {0xFEAA, {0x40, 0}, 1, Kind::Tracker, Conf::Strong,
   "Google Find My Device"},
  {0xFEAA, {0x41, 0}, 1, Kind::Tracker, Conf::Strong,
   "Google Find My Device"},

  /* IETF DULT, Detecting Unwanted Location Trackers, service 0xFCB2.
   *
   * The cross-vendor standard for this exact problem: a tracker separated
   * from its owner is supposed to advertise here so that any phone can warn
   * about it. One rule covers every vendor that implements it, present and
   * future, which makes it the highest-value row in this file.
   *
   * No prefix. The service alone is the signal, because nothing advertises
   * 0xFCB2 except something declaring itself a location tracker. */
  {0xFCB2, {0, 0}, 0, Kind::Tracker, Conf::Strong, "DULT tracker"},
};

/* ── BLE and WiFi: names matched anywhere ─────────────────────────────────
 *
 * The needles here are deliberately long. A substring has no anchor, so the
 * cost of a short one is paid in every unrelated device that happens to
 * contain those characters, and this table is checked after the prefix table
 * precisely so an anchored match wins first.
 *
 * minNameLen is the other guard, for needles that are a whole name on their
 * own: "Marauder" as an entire name is a deauther, and "Marauder" inside a
 * 40-character name is somebody who named their speaker after a Pineapple
 * hunting trip. It is a weak guard and it is honest about that in Conf.
 */
static const NameInSig kNameInSigs[] = {
  /* ── Fleet telematics ────────────────────────────────────────────────── */
  {"Samsara",       0, Kind::Vehicle, Conf::Strong, "Samsara fleet"},
  {"KeepTruckin",   0, Kind::Vehicle, Conf::Strong, "Motive ELD"},
  {"Motive Hotspot", 0, Kind::Vehicle, Conf::Strong, "Motive ELD"},
  {"Motive_",       0, Kind::Vehicle, Conf::Strong, "Motive ELD"},
  {"PNet",          0, Kind::Vehicle, Conf::Likely, "PeopleNet ELD"},

  /* ── Tyre sensors ────────────────────────────────────────────────────── */
  {"tsTPMS",        0, Kind::Vehicle, Conf::Strong, "Tesla tyre sensor"},
  {"TireCheck",     0, Kind::Vehicle, Conf::Strong, "TireCheck TPMS"},
  {"TPMS",          0, Kind::Vehicle, Conf::Likely, "TPMS sensor"},
  {"FOBO",          0, Kind::Vehicle, Conf::Likely, "FOBO TPMS"},

  /* ── The car population, Weak throughout. See the BleSig block. ──────── */
  {"TeslaWallConnector", 0, Kind::Vehicle, Conf::Weak, "Tesla charger"},
  {"Cybertruck",    0, Kind::Vehicle, Conf::Weak, "Tesla"},
  {"TeslaGW",       0, Kind::Vehicle, Conf::Weak, "Tesla"},
  {"myChevrolet",   0, Kind::Vehicle, Conf::Weak, "Chevrolet"},
  {"myCadillac",    0, Kind::Vehicle, Conf::Weak, "Cadillac"},
  {"myBuick",       0, Kind::Vehicle, Conf::Weak, "Buick"},
  {"myGMC",         0, Kind::Vehicle, Conf::Weak, "GMC"},
  {"Uconnect",      0, Kind::Vehicle, Conf::Weak, "Stellantis Uconnect"},
  {"Porsche_WLAN",  0, Kind::Vehicle, Conf::Weak, "Porsche"},
  {"Audi_MMI_",     0, Kind::Vehicle, Conf::Weak, "Audi MMI"},
  {"MBUX",          0, Kind::Vehicle, Conf::Weak, "Mercedes MBUX"},
  {"CARLINK-",      0, Kind::Vehicle, Conf::Weak, "CarPlay adapter"},
  {"CarPlay",       0, Kind::Vehicle, Conf::Weak, "CarPlay head unit"},
  {"Winegard",      0, Kind::Vehicle, Conf::Weak, "Winegard RV"},

  /* ── Roadside travel-time readers ────────────────────────────────────────
   *
   * These sit on poles counting Bluetooth and Wi-Fi addresses as they pass,
   * to work out how long a journey took. Not cameras, and squarely what this
   * screen is for: they exist to track the device in your pocket. */
  {"BlipTrack",      0, Kind::Alpr,   Conf::Strong, "BlipTrack"},
  {"BLIP-Track",     0, Kind::Alpr,   Conf::Strong, "BlipTrack"},
  {"BlueTOAD",       0, Kind::Alpr,   Conf::Strong, "BlueTOAD"},
  {"BlueARGUS",      0, Kind::Alpr,   Conf::Strong, "Iteris BlueARGUS"},
  {"VantageARGUS",   0, Kind::Alpr,   Conf::Strong, "Iteris VantageARGUS"},
  {"VantageVelocity", 0, Kind::Alpr,  Conf::Strong, "Iteris Vantage"},
  {"Vantage Velocity", 0, Kind::Alpr, Conf::Strong, "Iteris Vantage"},
  {"TrafficCast",    0, Kind::Alpr,   Conf::Strong, "TrafficCast"},

  /* ── Intersection and pole cameras ───────────────────────────────────── */
  {"Miovision",      0, Kind::Alpr,   Conf::Strong, "Miovision"},
  {"Pigvision",      0, Kind::Camera, Conf::Strong, "Pigvision"},
  {"ShotSpotter",    0, Kind::Alpr,   Conf::Strong, "ShotSpotter"},
  {"Shot Spotter",   0, Kind::Alpr,   Conf::Strong, "ShotSpotter"},
  {"SoundThinking",  0, Kind::Alpr,   Conf::Strong, "SoundThinking"},

  /* LiveView Technologies solar trailers. "LiveView" on its own is two
   * ordinary words, so the hyphenated forms carry the confidence. */
  {"LVT-",           0, Kind::Alpr,   Conf::Strong, "LVT trailer"},
  {"LVT_",           0, Kind::Alpr,   Conf::Strong, "LVT trailer"},
  {"LiveView",       0, Kind::Alpr,   Conf::Likely, "LVT trailer?"},

  /* Commercial CCTV. Likely rather than Strong: these are on shops and
   * warehouses as often as on anything municipal. */
  {"Dahua",          0, Kind::Camera, Conf::Likely, "Dahua"},
  {"Uniview",        0, Kind::Camera, Conf::Likely, "Uniview"},
  {"Uniarch",        0, Kind::Camera, Conf::Likely, "Uniview Uniarch"},
  {"UNV-",           0, Kind::Camera, Conf::Likely, "Uniview"},

  /* UniFi Protect cameras in BLE setup mode, which is the only time they
   * advertise. Not a UniFi access point, which is a different thing wearing
   * the same brand. */
  {"UVC G3 Instant", 0, Kind::Camera, Conf::Strong, "UniFi Protect"},
  {"UVC G4 Instant", 0, Kind::Camera, Conf::Strong, "UniFi Protect"},
  {"UVC G6 Instant", 0, Kind::Camera, Conf::Strong, "UniFi Protect"},

  /* ── Pole support gear ───────────────────────────────────────────────── */
  {"FS Ext",         0, Kind::Accessory, Conf::Likely, "FS pole battery"},
  {"PENGUIN",        0, Kind::Accessory, Conf::Weak,   "Penguin battery?"},

  /* ── Body-worn and in-car video ──────────────────────────────────────── */
  {"Digital Ally",   0, Kind::Bodycam, Conf::Strong, "Digital Ally"},
  {"DigitalAlly",    0, Kind::Bodycam, Conf::Strong, "Digital Ally"},
  {"FirstVu",        0, Kind::Bodycam, Conf::Strong, "Digital Ally FirstVu"},
  {"VuLink",         0, Kind::Bodycam, Conf::Strong, "Digital Ally VuLink"},
  {"EVO-HD",         0, Kind::Bodycam, Conf::Likely, "Digital Ally EVO-HD"},

  /* The routers that ride with them. Fourteen Cradlepoint model numbers
   * collapse to the brand: an IBR1700 is a Cradlepoint and the model is not
   * what makes it interesting. */
  {"Cradlepoint",    0, Kind::Accessory, Conf::Strong, "Cradlepoint"},
  {"AirLink",        0, Kind::Accessory, Conf::Likely, "Sierra AirLink"},

  /* ── Fixed home cameras ──────────────────────────────────────────────────
   *
   * A camera watching one place continuously, which is the thing this
   * screen is about, and also the thing on half the porches in the country.
   * Graded accordingly. The distinctive spelling carries the confidence and
   * the bare brand word does not. */
  {"ARLO_VMB_",      0, Kind::Camera, Conf::Strong, "Arlo base station"},
  {"Arlo",           0, Kind::Camera, Conf::Weak,   "Arlo?"},
  {"EufyCam",        0, Kind::Camera, Conf::Likely, "eufy"},
  {"Nest Cam",       0, Kind::Camera, Conf::Likely, "Nest Cam"},
  {"Nestcam",        0, Kind::Camera, Conf::Likely, "Nest Cam"},
  {"Nest-Hello",     0, Kind::Camera, Conf::Likely, "Nest doorbell"},
  {"Reolink",        0, Kind::Camera, Conf::Likely, "Reolink"},
  {"Tapo",           0, Kind::Camera, Conf::Weak,   "TP-Link Tapo?"},

  /* Pentest kit. Pueo had ALFA's OUI as a Weak "Pineapple radio?" guess,
   * with a long comment on why that is nearly worthless: the block covers
   * every ALFA adapter, and a Pineapple wearing a randomised MAC does not
   * show it at all. The management SSID is a much better signature, and
   * people put their own text in front of it, which is why this needs a
   * substring rather than a prefix. */
  {"Pineapple_",   0, Kind::Pentest, Conf::Strong, "Hak5 Pineapple"},
  {"WiFi Pineapple", 0, Kind::Pentest, Conf::Strong, "Hak5 Pineapple"},
  {"MarauderAP",   0, Kind::Pentest, Conf::Strong, "ESP32 Marauder"},
  {"Marauder",     0, Kind::Pentest, Conf::Likely, "ESP32 Marauder"},
  {"GhostESP",     0, Kind::Pentest, Conf::Strong, "GhostESP"},
  {"Deauther",     0, Kind::Pentest, Conf::Likely, "ESP8266 Deauther"},

  /* Plate readers and municipal cameras whose names carry the vendor in the
   * middle rather than at the front, which the prefix table cannot
   * express. */
  {"Rekor",        0, Kind::Alpr,    Conf::Likely, "Rekor (ALPR)"},
  {"Hayden",       0, Kind::Alpr,    Conf::Likely, "Hayden AI (ALPR)"},
  {"Avigilon",     0, Kind::Camera,  Conf::Likely, "Avigilon"},
  {"Wisenet",      0, Kind::Camera,  Conf::Likely, "Hanwha Wisenet"},
  {"Rhombus",      0, Kind::Camera,  Conf::Likely, "Rhombus"},
  {"i-PRO",        0, Kind::Camera,  Conf::Likely, "Panasonic i-PRO"},

  /* Body cameras. Axon is already a Strong OUI match; these three are the
   * other vendors in that market and have no OUI here. */
  {"Wolfcom",      0, Kind::Bodycam, Conf::Likely, "Wolfcom"},
  {"WatchGuard",   0, Kind::Bodycam, Conf::Likely, "WatchGuard Video"},
  {"Reveal",       8, Kind::Bodycam, Conf::Weak,   "Reveal Media?"},
};

constexpr size_t kMfgSigCount     = sizeof(kMfgSigs) / sizeof(kMfgSigs[0]);
constexpr size_t kSvcDataSigCount = sizeof(kSvcDataSigs) / sizeof(kSvcDataSigs[0]);
constexpr size_t kNameInSigCount  = sizeof(kNameInSigs) / sizeof(kNameInSigs[0]);
constexpr size_t kMacSigCount     = sizeof(kMacSigs) / sizeof(kMacSigs[0]);
constexpr size_t kOuiSigCount     = sizeof(kOuiSigs) / sizeof(kOuiSigs[0]);
constexpr size_t kBle128SigCount  = sizeof(kBle128Sigs) / sizeof(kBle128Sigs[0]);
constexpr size_t kSsidSigCount    = sizeof(kSsidSigs) / sizeof(kSsidSigs[0]);
constexpr size_t kBleSigCount     = sizeof(kBleSigs) / sizeof(kBleSigs[0]);
constexpr size_t kBleNameSigCount = sizeof(kBleNameSigs) / sizeof(kBleNameSigs[0]);

}  // namespace Spotter
