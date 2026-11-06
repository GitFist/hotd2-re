"""Build the native, data-only Naomi visual bank from the supplied USA cartridge.



Usage: python tools/import_naomi_visuals.py --rom hotd2.zip --pc-game GAME

       --out GAME/naomi_visuals.bank [--report import_report.json]



No emulator, BIOS, generated textures or original executable is needed at runtime.

The PC executable is an offline descriptor/model-ID reference only. Palette colors

come from the verified native program, and mesh transfers require source evidence.

"""

from pathlib import Path

from collections import defaultdict, Counter

from functools import lru_cache

import argparse, hashlib, itertools, json, math, re, struct, sys, zipfile, zlib

import numpy as np

import pol_parse as pol

import tex_export as tex

from prs import decompress



PROGRAM_SHA1='9e77980d1aa980c879886e53cc76a16d7a9d43a1'

MPR_SHA1=(

 '185a0eab68d86617cb6325d64c48a2dd4854622b','5483c3949e587bbcca7e8fc7db9aff4cd2a33f02',

 '8cdebdebabc88160f458e1e779d9ebb4e6a14523','d5fd4c0800b3a1ea04231018fcaba79184fa1d87',

 '4f56768f07703452ef92d183e4ee654ab9711283','7636661da0cc9bd5a1a2062f9f3ef65889c86fd5',

 '9f291c4dd9a049eeed88d80867f7fca1d15c6095','a2941b6afd0302822133d932064d1aad873b1c04',

 '21e35def0ed998a70cc982f373feb50b7974612a','7ec1b3c9c94882c5fe7b6ba6ffe9220e90824870',

 '981c65310c89e7a26e2b2c3e57623e78f6ad33d0','9257492fc3e48516897002dd3ff247093af27d87',

 'ffeb061b31027ac322c14b9050c686b2b844d2e1','b36d8e490ac477db178b8df08f7997448308d3fd',

 '6cb44f0a3f80eb68a218bba97b2395961c596b9c','e4b87a6c6fd6a2eeda8e0e5ae7bed01b18386e54',

 'f87d618231b71b65952fc7ea7ccabdd208622a00','77eb7061caaf0288aad04ed88c4247d27617f338',

 '68d7e9becefe27b556e0c5d7ba00efd2d1fb71ca','57936367fec0000691641525682fb8aefc4e4f56')

# Original matching chapter5/6 section rows, resource IDs and independently

# verified geometry-bound/sphere fingerprints. Chapter1–4 section lists differ;

# they deliberately have no inferred whole-object correspondence here.

STAGE_SOURCE_OBJECTS=((6296, 'st5.bin', 7, 18, 6867, 'a77f98f766eec7eeeaaecff58d84587fc537efae4dcf3181433697c212350287',

  'bef8da106672d1d551a85e077500f3774bc36423ca0d953353a51b3680759554', ((5, 0, 0),)),

 (6291, 'st5.bin', 2, 13, 6862, 'eeb793d44bbd55bdb34e124bd1770f87d40f01a42c2a81f372bb463ddbf3e4c5',

  'fe22dd209ac70b6dbec9473e48febc01ba6654d07bce20443e34a773fec7c01e', ((5, 1, 0),)),

 (6293, 'st5.bin', 4, 15, 6864, 'ce63047d658260d39258415bec304bf75fdbcd7b4e6cc364a233bfd21ec9b897',

  '94c746338db2eb2c7f0cb5f6e259bf666ff519515d9efce3d0ec481e33da7389', ((5, 2, 0),)),

 (6294, 'st5.bin', 5, 16, 6865, '48caf9cad57563115a1c4dbf881695d13bd87e306614bd7ead24e26f87df2868',

  '94a2c58f1f8470ce0b5eaf2f2fcb9f41b63add7ba69af166417abadcce7d5059', ((5, 3, 0),)),

 (6295, 'st5.bin', 6, 17, 6866, 'f689340b5eea5d1bd317bd1543f4316a84ec8bfefcd1665f8536cd39a7161440',

  'ddc1469b427dc6f04a5fa72b5e7abead1130c92863a2e50cfc0e55adc2b7f466', ((5, 4, 0),)),

 (6292, 'st5.bin', 3, 14, 6863, 'f8d32acd3b0bc2ef5c88cd26295ca000c4789502ce4a4666387b2e898f3c0901',

  'e719909c7e6bb4b6ba94f1a8b4b64952a3439f6ea1aece05cecd665cfc333e56', ((5, 5, 0),)),

 (6313, 'st5.bin', 10, 53, 6902, '442821a84e2c801ad1ee561b9ecb1a43622bbad960727751a4b4057a1f043e08',

  '27569af98b3b1a721cde1a70a35a079aa898f0bc858c39d8232e255a6917adb4', ((5, 6, 0),)),

 (6306, 'st5_02b.bin', 7, 28, 6877, '8908c0d30d2f05cc43928c1538e3d0476c95c73416e2fcba2cc6939615fedf78',

  '4fb4f6b69c0d3ab2a41c3aeecf78b5df1f6ace32727c9beb08c772fc0ea5e783', ((5, 7, 0), (6, 10, 0))),

 (6418, 'st6_01.bin', 8, 70, 7009, '5c740e64470e327af8773b8b6ad2ec0ce11cc8364145515ff9909c7a3b913a77',

  '519d3ef8d14fe12fe01e389660aa2acd9b8871df10570c65872d636b0fd6d1c1', ((5, 8, 1), (6, 11, 0))),

 (6422, 'st6_01.bin', 12, 74, 7013, 'bec5252c51d7d531e531f08e9f683472e77b7288e1d1452267244fe353c51501',

  'bec5252c51d7d531e531f08e9f683472e77b7288e1d1452267244fe353c51501', ((6, 0, 0),)),

 (6336, 'st6_01.bin', 0, 62, 6927, '68d096768494dbc073761889485381363328a2737dc6cca82df6a9f3aac00843',

  '68d096768494dbc073761889485381363328a2737dc6cca82df6a9f3aac00843', ((6, 1, 0),)),

 (6429, 'st6_01b.bin', 0, 81, 7020, '8ed17c67ecf8d9fdbaebca528e7c0f0b8e2fe55760d6c4d30871926b4b223095',

  '1efd094d9844fc31a5b63405d9ec3ce51c02b5db7e3ff8fc43bc6a44dbc06b14', ((6, 2, 0),)),

 (6430, 'st6_01b.bin', 1, 82, 7021, 'dd6952e69cd11883df6e18055b15921099c9199eeed3ab3ef1bd82bc662b55cd',

  'dd6952e69cd11883df6e18055b15921099c9199eeed3ab3ef1bd82bc662b55cd', ((6, 3, 0),)),

 (6432, 'st6_01b.bin', 3, 84, 7023, '13f9a6c1287def98bad0c6c3157c695c75f6379e0a8a0c64b2e320e80968a934',

  '9962160031902afc1eb834d5f67aa26d6249e43afdefb07b92639c7c2d9ab755', ((6, 4, 0),)),

 (6436, 'st6_01b.bin', 7, 88, 7027, '9659fc52bf6839b9beb1ed486ed46087cad8f80a4c8b5b722ff1a044f6dfb462',

  '9659fc52bf6839b9beb1ed486ed46087cad8f80a4c8b5b722ff1a044f6dfb462', ((6, 5, 0),)),

 (6425, 'st6_01.bin', 15, 77, 7016, '7820c4dd1611685aa1651056adcc664a0743a0f0ed8d70405d5b343479389b49',

  '7820c4dd1611685aa1651056adcc664a0743a0f0ed8d70405d5b343479389b49', ((6, 6, 0),)),

 (6423, 'st6_01.bin', 13, 75, 7014, 'd104343ab33bd6f0f58a1d7977db70ed30ffdd66fd9c85820eec470178771869',

  '9731d689872dae26a032706d8747a9429420aae7433cca6c9a5cb3779c601e65', ((6, 7, 0),)),

 (6415, 'st6_01.bin', 5, 67, 7006, '11654a949353ab90f1205aa5f2dd433eb7e305c5a80498889a388aa8bbe4ab5e',

  '11654a949353ab90f1205aa5f2dd433eb7e305c5a80498889a388aa8bbe4ab5e', ((6, 8, 0),)),

 (6424, 'st6_01.bin', 14, 76, 7015, 'c9f3a45e0f58c6671de2ccf10c0601db355f9ba65d78f76049a4ec3e031d3e93',

  '2e5ecf23cadebc36bcf134f3521e6345749ccc35c3af29acc8fbdfd23624fa8a', ((6, 9, 0),)))

# Authored spatial/role correspondences where source section lists differ.
# Explicit groups restore one original object from split/merged PC chunks.
SPATIAL_STAGE_OBJECTS=((6147, 'st1_04b.bin', 0, 96, 6723, 14, 'ee892904c97c7be538cebc1061eb1dd32dcc9817d1cff7dc70337243a0092106',
  '6637bdc62ad143ca081cf3f0992057821775a42afa54ddb19539df74da7620b0', ((1, 9, 0, (9,)), (2, 2, 0, (2,)))),
 (6148, 'st1_04b.bin', 1, 96, 6723, 14, '283afd469dd7a6423c0a878534b4f6d08a7930495f4440c7ca9643ad23f69323',
  '6637bdc62ad143ca081cf3f0992057821775a42afa54ddb19539df74da7620b0', ((1, 15, 0, (9,)), (2, 3, 0, (2,)))),
 (6156, 'st1_04c.bin', 0, 103, 6730, 14, 'd6d68c858cb3900b8533f06483899ee0ef2a6e92b5fc98c3e73860d65094cfaa',
  '716e9366f7e69cd5bbd5a9a70cc51576e14d10374f4317c1e03d8c6ce9d3cd5b', ((1, 10, 0, (10,)), (2, 34, 0, (34,)))),
 (6157, 'st1_04c.bin', 1, 103, 6730, 14, 'eed3dd16cf9b862d8e572b2b501b10b24ca6ecf5be4b72324b5000dbc1132fce',
  '716e9366f7e69cd5bbd5a9a70cc51576e14d10374f4317c1e03d8c6ce9d3cd5b', ((2, 55, 0, (34,)),)),
 (5956, 'st1_04.bin', 0, 59, 6515, 15, '85956132c7bffc44b7bee894e228a72fc9a2e379144b6043e9fb85b764a03120',
  'f613b71f95747aec3194edfef8fa2644634f518923e65199468280f82246ec0f', ((1, 11, 0, (11,)),)),
 (461, 'st2_06.bin', 0, 1, 529, 15, 'b2118c2fc0ad4250bd68c35ca0476136d0726a589a3215869abf7649d39bb2b7',
  'b2118c2fc0ad4250bd68c35ca0476136d0726a589a3215869abf7649d39bb2b7', ((2, 15, 0, (15,)),)),
 (6223, 'st3_03.bin', 2, 132, 6766, 15, '2311cebeb1102a96092eaa5dd081dc8fb2cb7daac56840bf8ce7fdc44ac46985',
  '432856e2117c908861f2b562863d0395f32dd39ad934cd06a0ae9868e52c5200', ((3, 17, 1, (17,)),)),
 (6742, 'st4_06.bin', 4, 185, 7505, 15, 'a4716056e5dcbb0fedae7483c94c075c52521f8f0b395f6ec449a4a463e37a2c',
  'bad3ad4a2e2043c4441af1ca99e48a513cb0619f744b9f64c492a2b2d0837111', ((4, 22, 1, (22,)), (4, 37, 0, (37,)))),
 (6745, 'st4_06.bin', 7, 188, 7508, 15, 'b29d8fd10c2d0a29a8daff76eda022311432e08e2bd878464021bf8eba519e7a',
  '3f9102b4d536b67601a2497200821525d0b2497c61b682f6b046d6a3d55f1463', ((4, 25, 0, (25,)),)),
 (6131, 'st2_07.bin', 5, 88, 6712, 14, 'd9233260aee5433f5122b6b427bdaba756812e507cb518c8cecb23ddb688e3a6',
  '4e44050571c1aaf39433e2fa2bbeffdf578bfb61b89100caa494272f9fb828b0', ((2, 24, 0, (24,)),)),
 (6129, 'st2_07.bin', 4, 86, 6710, 14, '9cf1ab48eece42b0ee2fab01477ae7e4cb3725b879661d75aa1e18f6baf2a31c',
  '8ee13f8947fe26dfd23985b3ec468a15c3f0cb751b7e49a04725eb6baad377dc', ((2, 25, 0, (25,)),)),
 (6180, 'st2_14.bin', 0, 88, 6712, 14, '3eff7e9570f6935de494a3adae147a4c8b639f9c1fe51c94e31ad4c316041653',
  '4e44050571c1aaf39433e2fa2bbeffdf578bfb61b89100caa494272f9fb828b0', ((2, 49, 0, (24,)),)))
SPATIAL_STAGE_TABLES=((1, 5722224, 5722536, 5722604, 312, 202573152, 202573440, 288, '80fc6bf978f264c6d01bc5f6bc033e215779e991a967d6859fd1d4f8fe2a324d',
  '962ad44e149f55ff6a882488120d782698ab6d2513b33ed879c7b8a5b775da01'),
 (2, 5722672, 5724088, 5724324, 1416, 202573496, 202574864, 1368, '5470f545b26cee5f9ca186d492c474a268b05eade39aee1bcbb8960275270569',
  'b0fd813f85b9cad29405e2d4fb5857e997ccf185559ae13e6e184722077f0c6d'),
 (3, 5724560, 5725184, 5725296, 624, 202575048, 202575600, 552, '2716c565a4948c005723f5add2079b7949f79339aad82c997db1343f6f6fd7a3',
  'ac4a037891d3a1c0cb49c0b063fda81a58f4ce36f29e74782f24e6ec5e706099'),
 (4, 5725408, 5726344, 5726500, 936, 202575700, 202576612, 912, '2462e34b332cce4d6e75066e999ef8a28586a8e15e1a88fbf9ab5d58ff1ce7ca',
  '6300289b2aad51c224471b2aef7cfa8c714b0483efed20711e42ee94d250ab69'))
# Original PC45/Native4A sixteen-node rig: four verified rigid bone sources.
JUDGMENT_RIGID_SOURCES=((699, 57, 27, 778, 1, '414abefc70b9ff6ab2eaae122a57d2e13e7d5bf62e8b85e8ed13e159f8f2949b',
  'fe678f996aa978e42c081db6744190ddbac8187e6db21926cf081e9fb8a019c1'),
 (696, 54, 24, 775, 9, 'b0aa25bd62ff3d687aea46c44c416407627ccd2dc8a968d5585d713d199d595a',
  'c5e2f0d3bacc35e90e95cc8fef8e3ae564db9d5bcc029bf627f2f9555c3893d8'),
 (673, 31, 1, 752, 12, 'e08ea1fb8ebfe424f0947f78b01c35748f36745c15dbf989375a2f025a498db1',
  '877dc1e09cd6a25d4934200f55c84a787782a4838507afe6434cad7dd8fadadb'),
 (672, 30, 0, 751, 15, 'a2622b717018aba0d2670df8b0938513ff11543842e0e03a68bd77513f5135cf',
  'fb52a6b8ea155762b2985bf34cc00ba2a718888d7b21e840e3f2018297061ce2'))
# Original PC4B/Native50 Magician node roles; phase surfaces are the unique
# complete ordered XYZ/UV/signed-normal match within the authored node family.
MAGICIAN_RIGID_SOURCES=(('boss5.bin', 0, 0, 15, 1098, 935, 0, '8d9337873db5c37a8cfd5d429e1a400fd0e74e53884c6a84193074164f67eed4', '2307b02234ab03b7c27d159c0f151515448dd1e62fce7e5d4ca796ed7256a318'),
 ('boss5b.bin', 41, 166, 11, 1263, 1100, 1, 'bd6e0d64512bd68add480ec9b844b40b352a88339332dce98f675237ae2d1b2a', '178c71c8d0f49a20a39715f298a1a35871e9cbf4d756caac0e4f16089d16d2fb'),
 ('boss5b.bin', 283, 658, 4, 1755, 1592, 1, '3d2ec7e0418afda04a95cc5455018f12fdb8322cae61ff30701cb69842c8ef88', 'cedf8e3d83fbe362bfdb9f62a2d2d81e3f8ee858873e6a55a9ff15ecc1870efa'))
# Selected Emperor surfaces: unique complete signed-normal source matches.
BOSS6_SIGNED_NORMAL_SOURCES=((133, 133, '4f9d0d74caca319f619ea9a65855c40e9316102ad23dbfce485ee81450bade91', 'a0090c5e08c656219b05cebd818bf1e1e261a328dec812934f4b06ecc4bdef80'),
 (134, 134, '7b8c8122b286443f44fd7565bc9ff15a3a6c45587202a2c35e139612c31e973a', '261f17b1c8f110e11a9b05e539c19f1e6313b7977e2793baf84892b5258c93d5'),
 (137, 137, 'c1b0b32fcf87428aa1a0835020e61f7832aa8a255df0ad58b2f0637b7722f10c', 'f4a2415c095a9c839e0cbe9effda13ecaf124fd5f64538a0343f485ab162f491'),
 (138, 138, 'e6fb08e1b80876e9bd52b3dbf01fc4d7f523ce7e4249540a6f16d6b450f62263', '8a84c7d7aee9945ae143fbbb1a714a59d52a82dfbae3a8ec2807e03ad624e470'),
 (139, 139, 'ebcf6bc6102af44cbf0593463fb1cbdfee179e3e0106f72f0b1e7650426c0871', '444c37fab8977518d78937e0f6c3cf9c3e2270de1329db2e37f10d6142c51d89'),
 (153, 153, 'a5cc24ce2708cbd6b81a37c4195a82524ba6b3b8d9d274be3d4869b4e7717bfe', 'b5972bb9c6446560fdfc9d85568ac6786768ecb66e5f4a165c24a654cc505fe5'),
 (154, 154, '98d39bc034b30fdbc506cd396283087814ffae7e72b37ece2638e6c7bc7ec12b', '2fb24ff66c362c94bd38772e22af5fc0ba125b9ea0ce4751b86c17f7bee21498'),
 (155, 155, '24d11416943c3f5b311e83b5d63aed53f83be40e0663b615ce75f144b7c8b30a', '6c1cf294343e242d431f9a16d5a12f7840458051a7587ee7b14cfa443855bba9'),
 (160, 160, 'd5e3f72db382fc40d64d9711eb86d78a3e82f7fdf3e69fb355f037d53b45e1e0', '6a1778ce7329972787e1846530cb74405d88916f260ba7bdb8eda4f04e36d6ee'),
 (161, 161, 'b4b786828b04b79c29f2513d09a7e8cb2ab96f1fd565759170908aa53a27e6b6', 'b72962e4c415086be181952259674c9b4d4dabfe233accb3b45b03ac35052484'),
 (164, 164, '2dc754eb83b16bc0f1d46e9a9e0a5d1d2845ee23e605f770ad0c08947b72cce3', '537b59ac5f77616af67039121ea3a937b3a0da9e1bb8b66f9b2fb1b72d5424e5'),
 (165, 165, '135ed0ef4b9956d34308b6a0d398816241c4e056c6421215e23728b80afcdc4a', 'c4be2fb3f225e20bb4748175738618d13213e301248e49bd06c37bb9a4c39766'),
 (166, 166, '64771560523207a1d60fd8507e690dc0da050f89479e75dcbced9bbe79956110', 'c67e0b32a89d0a66602cf63d03bd7a74424d58656fd92764bebab2218879be5a'),
 (180, 180, '53297838f3fe0d519cf5f58d1e1c1d06b147750766c9ba97255de9871c4a5bfd', '9a32347aeb7f34b12df3e66f20aea8c444522cb73e3c38c44770760d9932a084'),
 (181, 181, '570626a517af7eabeb9790a9682c9b33237f3af09926b65b1593c1a081905058', '904b5682891c8d411c523c7cf6510e476409e6f6e3680bef347aa25a5d2ef4b6'),
 (182, 182, '70d18bf46fde0db572a02254c11e3db65f8fad7c8daa69251137e138a114713d', '341330d3119a3aa772088dee6f9174a808f1de25f91b2765c67c864af8fd7f27'))
# Bounded actual-draw source closure. Digests cover complete decoded model data
# (header/materials/strip controls/ordered vertices), not just bounding boxes.
LIVE_RIGID_SOURCE_GROUPS=(
 ('boat_normals','komono_7.bin','komono_7.bin',((2,1),(7,6),(9,8)),21,'a6a1a6450d7306125fc2a4821f17c33b0f60e003da1af9ff37867061725de343','b305215cabe2376e64a00254991c4e62bc54f2ca49998680514dd209a9cc3686'),
 ('judgment_burning_cars','char_adv04.bin','car_1.bin',((8,8),),20,'c19adac06b4f11b1bea32722ab0e3e02283721eaaca636be278541d93691b43a','29e3ed22033bad45453114d0e84494d83121f1e0b79f9a16b8bc6dd204894cd3'),
 ('boat_shadow','common.bin','common.bin',((201,480),),20,'f065d1924415c30a25485d87965951965ff2b6454b480fa1d2c7efd0a60c55d8','a07014f5f7bb385daf342d0c1ee7cf5774cb1c08bab84391f56d5a34926b58c7'),
 ('glass_fragments','garasu.bin','garasu.bin',tuple((i,i+72)for i in range(40)),20,'3625fb7e432785b398fea8bb2350099662f33e1d0e7a33f960777dc60fd530f3','d4dc79dcc1cfbd623d079693e489cc57af73a6aebc5fd49fa7b8d4c6f78def69'),
 ('bridge6','st6_01.bin','st5.bin',((10,72),),20,'7ea5d282dac50c632f98882b375119a51e85c04dd28c746b6d01653a20f32cd4','e253bdfd011c5bd1f8f1a233b73d29dc992c21fa0ea84fc2b98815c7a3d73a8c'),
 ('static_kind12','st5_02b.bin','st5.bin',((1,22),(3,24),(5,26)),20,'0c91b58359f2c31666d1ae35478bef3534e96ffc17ac1aba879fe546609b4083','551657783aaed7beca9fa2949071f66881988355400ccb7de74659122186bb93'),
 ('identical_st6_props','komono_st6.bin','komono_st6.bin',((51,68),(55,71),(56,68)),22,'c0899a41a2b979c6e66269b9f3ff49aea364c20a258b48525b12969e6bc7fe8d','05e498356cb3cc89a76b77177cb45d5b137708d765352c059e52e5826b937505'),
 ('orb_spark','eff_2.bin','eff_2.bin',tuple((i,i+95)for i in range(79,84)),20,'f045e2b1762105c6595b5b5fa6dc113fefb45fb555dca2f0deae099a5197ec80','6fc4f5832e473f49f44f809ce9a2e08755f9af1cac34eba95fff8c9e71808a63'),
 ('magician_afterimage','boss5.bin','boss5.bin',((210,411),),20,'67d039d7364e4b6b97f67d860dac85df56cbecdad3624b997d30a801106b32ad','898333b6f1f3a4d3816fea2df89e784f6f4d8102c066cb9c7af1341a0d6882db'),
 ('civilian_limbs','hito_buru.bin','hito_buru.bin',((0,0),(1,1)),9,'888b71e1bc3630d08c14cbee09d3a8f81d1aeca3621a131e6a20c0a14e01f72c','360f1780434605dd30bc73a5c5dc32763637d162b1631c71bf445f0bf083f5ad'))
MOUNT_NODE_SOURCE=('d01f60bbd8130379e6d2775e86335375c864a1f0949c0b23dcf2aaa024150de8','aa26c0245fde7a0465793beed2bbe5aa14f1feafb7f8555f99aa17a053095fda')
BASE=0x0C020000

SUPPORTED=(1,3,5,7,9,13)



def source_palettes(program):

    # Native init0C09A3DE registers this table through0C0AF340. Each record is

    # {u16 format,u16 capacity,u32 source,u32 colors}; source colors are1555,

    # not the PC's subsequently quantized4444 surface palettes.

    result={}

    for index in range(2):

        fmt,capacity,pointer,count=struct.unpack_from('<HHII',program,0x0C1401E8-BASE+index*12)

        expected=(256,96)if index==0 else(16,16)

        if fmt!=0 or(capacity,count)!=expected:raise ValueError('Unexpected native palette table')

        offset=pointer-BASE

        if offset<0 or offset+count*2>len(program):raise ValueError('Native palette source outside program')

        result[index]=dict(format=fmt,capacity=capacity,colors=list(struct.unpack_from('<'+'H'*count,program,offset)),source=pointer)

    return result



def indices(data,d):

    w,h=d['w'],d['h'];square=min(w,h);layout=(d['fmt']>>8)&255;out=[]

    for y in range(h):

        for x in range(w):

            t=((x//square)if w>=h else(y//square))*square*square+tex.twiddle(x%square,y%square)

            value=data[d['offset']+(t//2 if layout==5 else t)]

            out.append((value>>(4*(t&1)))&15 if layout==5 else value)

    return out



def native_pixels(data,d,palettes):

    if((d['fmt']>>8)&255)in(5,7):

        bank=0 if((d['fmt']>>8)&255)==7 else 1;palette=palettes[bank];values=indices(data,d)

        if max(values,default=0)>=len(palette['colors']):raise ValueError('Native glyph uses an uninitialized palette entry')

        return d['w'],d['h'],tex.rgba_bytes([palette['colors'][i]for i in values],palette['format'])

    w,h,v=tex.decode_texture(data,d);return w,h,tex.rgba_bytes(v,d['fmt']&7)



def unpack(data):

    if len(data)>=8:

        want=struct.unpack_from('<I',data)[0]

        if 0<want<64*1024*1024:

            try:

                out=decompress(data[4:])

                if len(out)==want:return out,1

            except (ValueError,IndexError):pass

    return data,0



def iso(cart,base):

    p=cart[base+0x8000:base+0x8800]

    if p[1:6]!=b'CD001':raise ValueError('Missing expected Naomi ISO volume')

    extent=struct.unpack_from('<I',p,158)[0]*2048;size=struct.unpack_from('<I',p,166)[0]

    records=[];at=extent

    while at<extent+size:

        n=cart[base+at]

        if not n:at=(at//2048+1)*2048;continue

        r=cart[base+at:base+at+n];name=bytes(r[33:33+r[32]]).decode('ascii');at+=n

        if name in('\0','\1'):continue

        if r[25]&2:raise ValueError('Unexpected Naomi resource subdirectory')

        records.append((name,struct.unpack_from('<I',r,2)[0]*2048,struct.unpack_from('<I',r,10)[0]))

    names={}

    for name,off,size in records:

        if name.startswith('TRANS.TBL'):

            for line in bytes(cart[base+off:base+off+size]).decode('ascii').splitlines():

                m=re.match(r'F (\S+)\s+(\S+)',line)

                if m:names[m[1]]=m[2]

    return {names.get(n,n.split(';')[0].lower()):bytes(cart[base+o:base+o+s]) for n,o,s in records if not n.startswith('TRANS.TBL')}



def source_rom(path):

    with zipfile.ZipFile(path)as z:

        program=z.read('epr-21585.ic22')

        if hashlib.sha1(program).hexdigest()!=PROGRAM_SHA1:raise ValueError('Expected verified USA epr-21585.ic22 cartridge program')

        cart=bytearray(b'\xff'*0xA800000);cart[:len(program)]=program;cart[0x200000:0x200000+len(program)]=program

        for n in range(1,21):

            names=[name for name in z.namelist()if re.search(r'\.ic'+str(n)+r'(s)?$',name)]

            if len(names)!=1:raise ValueError(f'Missing or ambiguous cartridge chip IC{n}')

            data=z.read(names[0])

            if len(data)!=0x800000:raise ValueError(f'Invalid IC{n} size')

            if hashlib.sha1(data).hexdigest()!=MPR_SHA1[n-1]:raise ValueError(f'Unexpected cartridge chip IC{n} hash')

            cart[n*0x800000:(n+1)*0x800000]=data

    return program,iso(cart,0x800000),iso(cart,0x4800000)



def descriptors(program):

    banks={}

    for i in range(194):

        ptr=struct.unpack_from('<I',program,0xD84F4+i*4)[0]-BASE

        name=program[ptr:program.index(0,ptr)].decode('ascii')

        count=struct.unpack_from('<H',program,0x10AC2C+(i+1)*2)[0]

        at=struct.unpack_from('<I',program,0x10A920+(i+1)*4)[0]-BASE

        if not count:continue

        ds=[]

        for j in range(count):

            w,h,fmt,off,tid=struct.unpack_from('<HHIII',program,at+j*16)

            ds.append(dict(index=j,w=w,h=h,fmt=fmt,offset=off,id=tid))

        if name in banks and banks[name]!=ds:raise ValueError('Ambiguous native texture bank '+name)

        banks[name]=ds

    return banks



def supported(d):return (d['fmt']&7)<=2 and ((d['fmt']>>8)&255)in SUPPORTED



def models(data):

    data,_=unpack(data);headers=pol.read_header(data,pol.header_count(data));out={};bad=[]

    for i,(start,flag,root)in enumerate(headers[:-1]):

        if flag:bad.append(i);continue

        try:h,meshes=pol.parse_model(data,root,headers[i+1][0])

        except (ValueError,IndexError,struct.error,pol.FormatError):bad.append(i);continue

        if h['flags']&0x10:bad.append(i);continue

        v=np.asarray([v for m in meshes for _,_,vs in m.strips for v in vs],dtype=np.float32).reshape(-1,8)

        if not len(v):continue

        out[i]=dict(header=h,meshes=meshes,v=v,top=tuple(tuple((k,len(vs))for _,k,vs in m.strips)for m in meshes))

    return out,bad



def delta(a,b):return float(np.max(np.abs(a.astype(np.float64)-b.astype(np.float64))))if a.size else 0.



def triangle_stream(top,vertices):

    at=0

    for kind,count in top:

        v=vertices[at:at+count];at+=count

        for i in range(0,count-2,3 if kind=='list'else 1):

            triangle=v[i:i+3]

            if float(np.dot(np.cross(triangle[1,:3]-triangle[0,:3],triangle[2,:3]-triangle[0,:3]),np.cross(triangle[1,:3]-triangle[0,:3],triangle[2,:3]-triangle[0,:3])))>1e-20:yield triangle[:,[0,1,2,6,7]]



def supported_model(m,bank,descs):

    ds=descs.get(bank,[])

    return all(x.material[8]==0xFFFFFFFF or(x.material[8]<len(ds)and supported(ds[x.material[8]]))for x in m['meshes'])



def main():

    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--rom',type=Path,required=True);ap.add_argument('--pc-game',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--report',type=Path)

    args=ap.parse_args();program,ntex,npol=source_rom(args.rom);nd=descriptors(program);palettes=source_palettes(program);exe=tex.Exe(str(args.pc_game/'Hod2.exe'));pd={};pcfiles={}

    for file_index,name,ptr,count in tex.texture_files(exe):

        if name not in pd:pd[name]=list(tex.descriptors(exe,ptr,count));pcfiles[name]=file_index

    aliases=defaultdict(set);modelmap={};meshmap={};higher_contexts=defaultdict(list);modelReasons=Counter();coverage=[];unsupported=[];modelMaterialIndices={};native_materials={};native_mesh_counts={};environment_index=defaultdict(list)

    # Whole bank identity gives a descriptor-index correspondence independent of

    # geometry. Dimensions/format/offset must agree; relocated IDs may differ.

    for name,ns in nd.items():

        ps=pd.get(name,[]);pfile=args.pc_game/'tex'/name

        if pfile.exists()and name in ntex and pfile.read_bytes()==ntex[name]and len(ns)==len(ps):

            for n,p in zip(ns,ps):

                if supported(n)and all(n[k]==p[k]for k in('w','h','fmt','offset')):aliases[name,p['index']].add((name,n['index']))

    # Match local geometry and UVs, never same-named whole containers by ordinal.

    for name,raw in sorted(npol.items()):

        pfile=args.pc_game/'pol'/name

        try:nm,nb=models(raw)

        except (ValueError,IndexError,struct.error,pol.FormatError)as e:unsupported.append({'bank':name,'reason':str(e)});continue

        for ni,n in nm.items():

            native_mesh_counts[name,ni]=len(n['meshes'])

            for mi,m in enumerate(n['meshes']):

                native_materials[name,ni,mi]=m.material

                if not supported_model(n,name,nd):continue

                v=np.asarray([v for _,_,vs in m.strips for v in vs],dtype=np.float32).reshape(-1,8)

                if not len(v):continue

                top=tuple((k,len(vs))for _,k,vs in m.strips);cell=tuple(math.floor(float(x))for x in v[0,:3])

                environment_index[top,cell].append((name,ni,mi,m.material,v))

        if not pfile.exists():continue

        try:pm,pb=models(pfile.read_bytes())

        except (ValueError,IndexError,struct.error,pol.FormatError)as e:unsupported.append({'bank':name,'reason':str(e)});continue

        native_by_top=defaultdict(list)

        for ni,n in nm.items():native_by_top[n['top']].append((ni,n))

        matched=0

        for pi,p in pm.items():

            equal=[]

            for ni,n in native_by_top[p['top']]:

                dp=delta(n['v'][:,:3],p['v'][:,:3])

                if dp<=.001:equal.append((ni,n,dp))

            uv=[(ni,n,dp)for ni,n,dp in equal if delta(n['v'][:,6:],p['v'][:,6:])<=1e-5]

            if len(uv)==1:

                ni,n,dp=uv[0];native_part=ni;matched+=1

                if supported_model(n,name,nd):

                    modelmap[name,pi]=(name,ni,1)

                    modelMaterialIndices[name,pi]=[((x.material[9]&255)^128)-128 for x in n['meshes']]

                for mi,(a,b)in enumerate(zip(n['meshes'],p['meshes'])):

                    if supported_model(n,name,nd):meshmap[name,pi,mi]=(name,native_part,mi,1)

                    ni,pii=a.material[8],b.material[8]

                    if ni!=0xFFFFFFFF and pii!=0xFFFFFFFF and ni<len(nd.get(name,[]))and pii<len(pd.get(name,[]))and supported(nd[name][ni]):

                        aliases[name,pii].add((name,ni))

                        d,pdsc=nd[name][ni],pd[name][pii]

                        if d['w']*d['h']>pdsc['w']*pdsc['h']:higher_contexts[name,pii,name,ni].append((pi,mi,native_part,mi))

        # These two same-order part banks were independently checked during the

        # comparison: decimation affects David's heads and Zeal's corresponding

        # body parts; local bone frames/counts are retained. Other changed banks

        # are NOT inferred safe simply because their model counts agree.

        explicit=name in('znsam2.bin','boss1z.bin')and len(nm)==len(pm)and not nb and not pb

        if explicit:

            for pi,p in pm.items():

                n=nm.get(pi)

                if n is None or not supported_model(n,name,nd):continue

                nc=(n['v'][:,:3].min(0)+n['v'][:,:3].max(0))*.5;pc=(p['v'][:,:3].min(0)+p['v'][:,:3].max(0))*.5

                size=max(float(np.linalg.norm(p['v'][:,:3].max(0)-p['v'][:,:3].min(0))),.001)

                if float(np.linalg.norm(nc-pc))>max(.05,size*.05):continue

                modelmap[name,pi]=(name,pi,2)

                modelMaterialIndices[name,pi]=[((x.material[9]&255)^128)-128 for x in n['meshes']]

                if len(p['meshes'])==len(n['meshes']):

                    for mi,(a,b)in enumerate(zip(p['meshes'],n['meshes'])):

                        av=np.asarray([v for _,_,vs in a.strips for v in vs],dtype=np.float32).reshape(-1,8);bv=np.asarray([v for _,_,vs in b.strips for v in vs],dtype=np.float32).reshape(-1,8)

                        if tuple((k,len(vs))for _,k,vs in a.strips)==tuple((k,len(vs))for _,k,vs in b.strips)and delta(av[:,:3],bv[:,:3])<=.001 and delta(av[:,6:],bv[:,6:])<=1e-5:meshmap[name,pi,mi]=(name,pi,mi,4)

        coverage.append({'bank':name,'pc_parts':len(pm),'naomi_parts':len(nm),'unique_geometry_uv':matched,'mapped_rigid_parts':sum(k[0]==name for k in modelmap),'unsupported_naomi_parts':nb,'unsupported_pc_parts':pb})

        if name.startswith(('boss','car_','znsam')):print(name,'mapped',coverage[-1]['mapped_rigid_parts'],'/',len(pm),flush=True)

    # Repeated quads intentionally excluded by the generic uniqueness rule have

    # independently established effect-family bindings. Still verify coordinates

    # and UVs against the supplied inputs before accepting these named mappings.

    nm,_=models(npol['common.bin']);pm,_=models((args.pc_game/'pol/common.bin').read_bytes())

    effect_models={123:289,124:290,125:291,126:292,371:749,200:479}

    effect_models.update({i:i+166 for i in range(116,123)})

    # Each player has ten primary and ten secondary animation records. Their

    # geometry repeats across players, but the warm/blue materials differ, so

    # geometry-only uniqueness must not bind P2's first frame to the P1 family.

    for pc_start,native_start in ((40,149),(50,159),(142,386),(156,400)):

        effect_models.update({pc_start+i:native_start+i for i in range(10)})

    for pi,ni in effect_models.items():

        n,p=nm.get(ni),pm.get(pi)

        # The damage-overlay vertices were rounded at slightly different source

        # precision; the greatest established coordinate delta is 0.001683.

        position_tolerance=.002 if 116<=pi<=126 else .001

        if n is None or p is None or n['top']!=p['top']or delta(n['v'][:,:3],p['v'][:,:3])>position_tolerance or delta(n['v'][:,6:],p['v'][:,6:])>.001 or not supported_model(n,'common.bin',nd):

            raise ValueError(f'Verified effect binding does not match input: PC{pi}/Naomi{ni}')

        modelmap['common.bin',pi]=('common.bin',ni,3)

        modelMaterialIndices['common.bin',pi]=[((x.material[9]&255)^128)-128 for x in n['meshes']]

        for mi in range(len(n['meshes'])):meshmap['common.bin',pi,mi]=('common.bin',ni,mi,3)

    higher_contexts['common.bin',36,'common.bin',53].append((371,0,749,0))

    # The original growth/hold/fade callbacks select181C+(phase%30), resolving

    # to these native parts. Later records retain different strip packaging and

    # degenerate connectors; extra stored vertices do not imply extra surfaces.

    # Validate the shared local frame and UV extents without forcing old topology.

    for pi in range(338,368):

        ni=pi+338;p,n=pm[pi],nm[ni]

        position=max(delta(p['v'][:,:3].min(0),n['v'][:,:3].min(0)),delta(p['v'][:,:3].max(0),n['v'][:,:3].max(0)))

        uv=max(delta(p['v'][:,6:].min(0),n['v'][:,6:].min(0)),delta(p['v'][:,6:].max(0),n['v'][:,6:].max(0)))

        if position>.001 or uv>1e-5 or not supported_model(n,'common.bin',nd)or any(m.material[8]!=37 or m.material[2]!=0x20080452 for m in n['meshes']):raise ValueError('Native opaque ripple phase does not match verified source family')

        modelmap['common.bin',pi]=('common.bin',ni,4);modelMaterialIndices['common.bin',pi]=[((m.material[9]&255)^128)-128 for m in n['meshes']]

        for mi,(a,b)in enumerate(zip(p['meshes'],n['meshes'])):

            av=np.asarray([v for _,_,vs in a.strips for v in vs],dtype=np.float32).reshape(-1,8);bv=np.asarray([v for _,_,vs in b.strips for v in vs],dtype=np.float32).reshape(-1,8)

            if tuple((k,len(vs))for _,k,vs in a.strips)==tuple((k,len(vs))for _,k,vs in b.strips)and delta(av[:,:3],bv[:,:3])<=.001 and delta(av[:,6:],bv[:,6:])<=1e-5:meshmap['common.bin',pi,mi]=('common.bin',ni,mi,1)

    # Native polygonal hit blood is a separate58-frame opaque sequence. Derive

    # the exact non-linear order from the original program ID table, never616+i.

    common_count=pol.header_count(unpack(npol['common.bin'])[0]);common_ids=struct.unpack_from('<'+'H'*common_count,program,0x0C1003A8-BASE)

    polygon_ids=struct.unpack_from('<58H',program,0x0C0E0438-BASE);polygon_parts=[]

    for frame,global_id in enumerate(polygon_ids):

        if common_ids.count(global_id)!=1:raise ValueError('Ambiguous original polygon-blood model ID')

        part=common_ids.index(global_id);source=nm.get(part)

        expected_vertices=244 if part==660 else 242

        if source is None or len(source['v'])!=expected_vertices or not supported_model(source,'common.bin',nd)or any(m.material[8]!=54 for m in source['meshes']):raise ValueError('Unsupported original polygon-blood source record')

        modelmap['__naomi_polygon_blood__.bin',frame]=('common.bin',part,5);modelMaterialIndices['__naomi_polygon_blood__.bin',frame]=[((m.material[9]&255)^128)-128 for m in source['meshes']];polygon_parts.append(part)

    for row in coverage:row['mapped_rigid_parts']=sum(k[0]==row['bank']for k in modelmap)

    # Original sky-table entry0 selects native common737, not a same-name stage

    # container ordinal. Authored native UVs differ, while its topology/local

    # positions agree. This is a semantic source-table binding, not a UV guess.

    sky_pc,_=models((args.pc_game/'pol/st1_1.bin').read_bytes());sky=nm[737];old_sky=sky_pc[20]

    if sky['top']!=old_sky['top']or delta(sky['v'][:,:3],old_sky['v'][:,:3])>.001 or not supported_model(sky,'common.bin',nd):raise ValueError('Original native sky-table binding differs')

    modelmap['st1_1.bin',20]=('common.bin',737,6);modelMaterialIndices['st1_1.bin',20]=[((m.material[9]&255)^128)-128 for m in sky['meshes']]

    for mi,(a,b)in enumerate(zip(old_sky['meshes'],sky['meshes'])):

        meshmap['st1_1.bin',20,mi]=('common.bin',737,mi,6)

        if a.material[8]!=0xFFFFFFFF and b.material[8]!=0xFFFFFFFF:

            aliases['st1_1.bin',a.material[8]].add(('common.bin',b.material[8]))

            p,d=pd['st1_1.bin'][a.material[8]],nd['common.bin'][b.material[8]]

            if d['w']*d['h']>p['w']*p['h']:higher_contexts['st1_1.bin',a.material[8],'common.bin',b.material[8]].append((20,mi,737,mi))

    semantic_native={};semantic_pc={}

    def semantic_binding(pc_bank,pc_part,native_bank,native_part,position_tolerance=.001,uv_tolerance=1e-5,rigid_bounds=False):

        if native_bank not in semantic_native:

            semantic_native[native_bank]=models(npol[native_bank])[0]

            for part,source in semantic_native[native_bank].items():

                native_mesh_counts[native_bank,part]=len(source['meshes'])

                for mi,mesh in enumerate(source['meshes']):native_materials[native_bank,part,mi]=mesh.material

        if pc_bank not in semantic_pc:semantic_pc[pc_bank]=models((args.pc_game/'pol'/pc_bank).read_bytes())[0]

        pc=semantic_pc[pc_bank][pc_part];source=semantic_native[native_bank][native_part]

        if rigid_bounds:

            valid=len(pc['meshes'])==len(source['meshes'])and max(delta(pc['v'][:,:3].min(0),source['v'][:,:3].min(0)),delta(pc['v'][:,:3].max(0),source['v'][:,:3].max(0)))<=position_tolerance

        else:valid=pc['top']==source['top']and delta(pc['v'][:,:3],source['v'][:,:3])<=position_tolerance and delta(pc['v'][:,6:],source['v'][:,6:])<=uv_tolerance

        if not valid or not supported_model(source,native_bank,nd):raise ValueError(f'Original callback mesh binding differs: {pc_bank}/{pc_part}')

        modelmap[pc_bank,pc_part]=(native_bank,native_part,9 if rigid_bounds else 8);modelMaterialIndices[pc_bank,pc_part]=[((m.material[9]&255)^128)-128 for m in source['meshes']]

        for mi,(a,b)in enumerate(zip(pc['meshes'],source['meshes'])):

            av=np.asarray([v for _,_,vs in a.strips for v in vs],dtype=np.float32).reshape(-1,8);bv=np.asarray([v for _,_,vs in b.strips for v in vs],dtype=np.float32).reshape(-1,8)

            if tuple((k,len(vs))for _,k,vs in a.strips)==tuple((k,len(vs))for _,k,vs in b.strips)and delta(av[:,:3],bv[:,:3])<=.001 and delta(av[:,6:],bv[:,6:])<=.001:meshmap[pc_bank,pc_part,mi]=(native_bank,native_part,mi,8)

            if a.material[8]==0xFFFFFFFF or b.material[8]==0xFFFFFFFF:continue

            aliases[pc_bank,a.material[8]].add((native_bank,b.material[8]));p,d=pd[pc_bank][a.material[8]],nd[native_bank][b.material[8]]

            if d['w']*d['h']>p['w']*p['h']:higher_contexts[pc_bank,a.material[8],native_bank,b.material[8]].append((pc_part,mi,native_part,mi))

    # Original shutter and intact/brokenprop callbacks identify these models

    # across the port's regrouped containers; geometry/UV streams confirm them.

    semantic_binding('komono_st1.bin',0,'etc_1.bin',7)

    semantic_binding('komono_st1.bin',9,'etc_door.bin',24)

    semantic_binding('komono_st1.bin',10,'etc_door.bin',25)

    semantic_binding('garasu.bin',72,'etc_1.bin',62)

    semantic_binding('garasu.bin',73,'etc_1.bin',63)

    semantic_binding('st5_01b.bin',4,'st5.bin',45)

    semantic_binding('st5_01b.bin',5,'st5.bin',49)

    semantic_binding('st1_1.bin',2,'st1_1.bin',29)

    semantic_binding('st1_1b.bin',14,'st1_1.bin',152)

    semantic_binding('komono_st4.bin',2,'st1_1.bin',84)

    semantic_binding('komono_st6.bin',52,'komono_st6.bin',69)

    for pc_part,native_part in ((1,30),(4,33),(5,34),(7,36),(8,37),(9,38)):semantic_binding('komono_kanban.bin',pc_part,'etc_2.bin',native_part)

    semantic_binding('komono_kanban.bin',11,'st1_1.bin',81)

    for pc_part,native_part in ((4,11),(5,12),(6,13),(7,14),(8,15),(9,16)):semantic_binding('komono_bar.bin',pc_part,'komono_bar.bin',native_part,.032,.0011)

    semantic_binding('komono_bar.bin',0,'etc_door.bin',0,uv_tolerance=.001)

    semantic_binding('komono_st4.bin',0,'etc_1.bin',23,uv_tolerance=.001)

    semantic_binding('char_adv04.bin',12,'car_1.bin',13)

    for pc_part,native_part in ((39,9),(33,3),(34,4),(61,31),(62,32)):semantic_binding('char_adv04.bin',pc_part,'boss1z.bin',native_part,.04,rigid_bounds=True)

    for part in range(8):semantic_binding('char_adv04.bin',63+part,'boss1z_wing.bin',part)

    # The three original breakable-prop skeleton roots have explicit indexed

    #38-node correspondence. Preserve each variant's distinct native artwork.

    for part in range(114):semantic_binding('komono_1.bin',part,'komono_1.bin',part+1,uv_tolerance=.001)

    # Original initialized Hydra skeletons prove these separate rigid bone draws;

    # new jaw topology is retained, never forced onto PC skin-offset records.

    for pc_part,native_part in ((0,13),(1,14),(3,16),(4,17)):semantic_binding('boss3l.bin',pc_part,'boss3l.bin',native_part,.001,rigid_bounds=True)

    for pc_part,native_part in ((0,5),(1,6),(3,8)):semantic_binding('boss3.bin',pc_part,'boss3.bin',native_part,.04,rigid_bounds=True)

    # Split PC stage banks use world-space environment meshes from merged native

    # banks. Index by topology and the first position's unit cell; probing only

    # the27 adjacent cells handles source float rounding without quadratic scans.

    @lru_cache(maxsize=8)

    def decoded_native_texture_bank(bank):return unpack(ntex[bank])[0]

    blood_ids={d['id']for d in nd['scr_blood_red.bin']}

    @lru_cache(maxsize=None)

    def native_texture_identity(bank,index):

        if index==0xFFFFFFFF:return None

        d=nd[bank][index];w,h,pixels=native_pixels(decoded_native_texture_bank(bank),d,palettes)

        return w,h,hashlib.sha256(pixels).digest(),d['id']if d['id']in blood_ids else None

    split_coverage=[];nonstage_coverage=[];identical_candidates_collapsed=0;stage_fragments=[];ambiguous_geometry_texture_aliases=0

    for path in sorted((args.pc_game/'pol').glob('*.bin')):

        match=re.fullmatch(r'st([1-6])_(?:\d{2}[a-z]?|tika_bos)\.bin',path.name)

        chapter=int(match[1])if match else 0

        try:parts,bad=models(path.read_bytes())

        except (ValueError,IndexError,struct.error,pol.FormatError):continue

        total=mapped=ambiguous=0;samples=[];source_banks=Counter()

        for part,p in parts.items():

            if not match and(path.name,part)in modelmap:continue

            for mi,m in enumerate(p['meshes']):

                total+=1

                if(path.name,part,mi)in meshmap:mapped+=1;source_banks[meshmap[path.name,part,mi][0]]+=1;continue

                v=np.asarray([v for _,_,vs in m.strips for v in vs],dtype=np.float32).reshape(-1,8)

                if not len(v):continue

                top=tuple((k,len(vs))for _,k,vs in m.strips);cell=tuple(math.floor(float(x))for x in v[0,:3]);found=[]

                for offset in itertools.product((-1,0,1),repeat=3):

                    key=tuple(x+y for x,y in zip(cell,offset))

                    for native_bank,ni,nmi,material,nv in environment_index.get((top,key),()):

                        if delta(v[:,:3],nv[:,:3])<=.001 and delta(v[:,6:],nv[:,6:])<=.001:found.append((native_bank,ni,nmi,material,nv))

                if 1<len(found)<=32:

                    visual_keys={(hashlib.sha256(candidate[4].tobytes()).digest(),candidate[3][:8]+candidate[3][9:],native_texture_identity(candidate[0],candidate[3][8]))for candidate in found}

                    if len(visual_keys)==1:found=found[:1];identical_candidates_collapsed+=1

                if len(found)>1 and m.material[8]!=0xFFFFFFFF and m.material[8]<len(pd.get(path.name,[])):

                    textures={(candidate[0],candidate[3][8])for candidate in found if candidate[3][8]!=0xFFFFFFFF}

                    if len(textures)and all(candidate[3][8]!=0xFFFFFFFF for candidate in found)and len({native_texture_identity(*ref)for ref in textures})==1:

                        aliases[path.name,m.material[8]].update(textures);ambiguous_geometry_texture_aliases+=1

                if len(found)==1:

                    native_bank,ni,nmi,material,_=found[0];meshmap[path.name,part,mi]=(native_bank,ni,nmi,7);mapped+=1;source_banks[native_bank]+=1

                    pi,ni_tex=m.material[8],material[8]

                    if pi!=0xFFFFFFFF and ni_tex!=0xFFFFFFFF and pi<len(pd.get(path.name,[]))and ni_tex<len(nd[native_bank]):

                        aliases[path.name,pi].add((native_bank,ni_tex));pdesc,ndesc=pd[path.name][pi],nd[native_bank][ni_tex]

                        if ndesc['w']*ndesc['h']>pdesc['w']*pdesc['h']:higher_contexts[path.name,pi,native_bank,ni_tex].append((part,mi,ni,nmi))

                else:

                    ambiguous+=len(found)>1

                    if len(samples)<4:samples.append(dict(part=part,mesh=mi,texture=m.material[8],vertices=len(v),candidates=len(found)))

                    if match and m.material[8]!=0xFFFFFFFF:stage_fragments.append((path.name,m.material[8],top,v))

        if total:

            row=dict(bank=path.name,chapter=chapter,native_banks=dict(source_banks),meshes=total,mapped=mapped,ambiguous=ambiguous,unresolved=total-mapped,unresolved_samples=samples)

            (split_coverage if match else nonstage_coverage).append(row)

    # Actual rigid stage draws have individually proven source counterparts just

    # beyond the general export-rounding cutoff. The first wraps one original UV

    # by a whole tile; the shop/window and wall faces retain their ordered UVs.

    # Require a unique complete source stream under each local bound, preserving

    # its authored material/vertices/UVs without relaxing the deformation gate.

    rigid_uv_bindings=[]

    for pc,part,mesh,native,npart,nmesh,position_limit,uv_limit in [

        ('st1_03c.bin',2,40,'st1_1.bin',80,35,.001,None),

        ('st1_04b.bin',1,94,'st1_1.bin',96,170,.005,.00004),

        ('st1_04b.bin',1,99,'st1_1.bin',96,174,.005,.00004),

        ('st3_03.bin',2,19,'st1_1.bin',132,16,.0025,.000001),

        ('st2_06.bin',0,63,'st1_1.bin',1,63,.025,.000001),

        ('st2_06.bin',0,64,'st1_1.bin',1,64,.025,.000001),

        ('st2_06.bin',0,65,'st1_1.bin',1,65,.025,.000001)]:

        p=models((args.pc_game/'pol'/pc).read_bytes())[0][part]['meshes'][mesh]

        pv=np.asarray([v for _,_,vs in p.strips for v in vs],dtype=np.float32).reshape(-1,8)

        top=tuple((k,len(vs))for _,k,vs in p.strips);cell=tuple(math.floor(float(x))for x in pv[0,:3]);found=[]

        for shift in itertools.product((-1,0,1),repeat=3):

            for bank,ni,mi,m,nv in environment_index.get((top,tuple(x+y for x,y in zip(cell,shift))),()):

                if delta(pv[:,:3],nv[:,:3])<=position_limit and(uv_limit is None or delta(pv[:,6:],nv[:,6:])<=uv_limit):found.append((bank,ni,mi,m,nv))

        if len(found)!=1 or found[0][:3]!=(native,npart,nmesh):raise ValueError('Verified rigid native UV source is no longer unique')

        _,_,_,m,nv=found[0];meshmap[pc,part,mesh]=(native,npart,nmesh,10)

        if m[8]!=0xFFFFFFFF and p.material[8]!=0xFFFFFFFF:

            aliases[pc,p.material[8]].add((native,m[8]));pdesc,ndesc=pd[pc][p.material[8]],nd[native][m[8]]

            if ndesc['w']*ndesc['h']>pdesc['w']*pdesc['h']:higher_contexts[pc,p.material[8],native,m[8]].append((part,mesh,npart,nmesh))

        rigid_uv_bindings.append(dict(bank=pc,part=part,mesh=mesh,nativeBank=native,nativePart=npart,nativeMesh=nmesh,maxPositionDelta=delta(pv[:,:3],nv[:,:3]),maxUvDelta=delta(pv[:,6:],nv[:,6:]),positionLimit=position_limit,uvLimit=uv_limit))

    # This merged PC surface is a subset of one larger original native mesh.

    # Every source point has a single bit-identical native UV, while seam normals

    # differ. Export the original source with evidence12: Scene may copy only

    # its UV/material onto the PC topology, never draw all444 native vertices or

    # choose an arbitrary source normal. No global texture alias: PC UVs differ.

    pc='st2_14.bin';native='st1_1.bin';part=0;mesh=101;npart=86;nmesh=188

    p=models((args.pc_game/'pol'/pc).read_bytes())[0][part]['meshes'][mesh]

    m=models(npol[native])[0][npart]['meshes'][nmesh]

    pv=np.asarray([v for _,_,vs in p.strips for v in vs],dtype=np.float32).reshape(-1,8)

    nv=np.asarray([v for _,_,vs in m.strips for v in vs],dtype=np.float32).reshape(-1,8)

    if(len(pv),len(nv),p.material[8],m.material[8])!=(128,444,29,591):raise ValueError('Verified native source-UV surface layout changed')

    position_delta=0.

    for vertex in pv:

        differences=np.max(np.abs(nv[:,:3].astype(np.float64)-vertex[:3]),axis=1);candidates=nv[differences<=.0025]

        if not len(candidates)or not np.all(candidates[:,6:].view(np.uint32)==candidates[0,6:].view(np.uint32)):raise ValueError('Original source point has missing or ambiguous UVs')

        position_delta=max(position_delta,float(differences.min()))

    meshmap[pc,part,mesh]=(native,npart,nmesh,12)

    higher_contexts[pc,p.material[8],native,m.material[8]].append((part,mesh,npart,nmesh))

    rigid_uv_material_bindings=[dict(bank=pc,part=part,mesh=mesh,nativeBank=native,nativePart=npart,nativeMesh=nmesh,sourceVertices=len(nv),targetVertices=len(pv),positionLimit=.0025,maxPositionDelta=position_delta)]

    # Identical original section lists and object rows identify nineteen whole

    # stage objects independently of geometry. Native bounds genuinely expand

    # for several objects, so compare each side to its verified fingerprint

    # rather than clipping the original source to the simplified PC bounds.

    original=lambda address,size:program[address-BASE:address-BASE+size]

    stage_tables={5:(0x005761C0,0x005762B0,0x005762B0,0x0C13137C,0x0C13146C,0xF0),6:(0x005762D4,0x00576424,0x00576454,0x0C131490,0x0C1315E0,0x150)}

    for chapter,(pc_sections,_,_,native_sections,native_objects,size)in stage_tables.items():

        if original(native_sections,size)!=exe.bytes(pc_sections,size):raise ValueError('Original section lists differ')

        if struct.unpack('<I',original(0x0C131954+(chapter-1)*4,4))[0]!=native_sections or struct.unpack('<I',original(0x0C13197C+(chapter-1)*4,4))[0]!=native_objects:raise ValueError('Original stage table pointer differs')

    if struct.unpack('<H',original(0x0C1050A0+154*2,2))[0]!=89:raise ValueError('Original native stage model-ID list differs')

    pc_model_ids={name:ids for _,name,ids in pol.pol_files(exe)if ids}

    def bounds_fingerprint(model):

        v=model['v'][:,:3]

        return hashlib.sha256(struct.pack('<II10f',len(model['meshes']),len(v),*v.min(0),*v.max(0),*model['header']['sphere'])).hexdigest()

    whole_stage_bindings=[];whole_stage_sources={};stale_stage_meshes=[]

    for pc_id,pc,part,npart,native_id,pc_fingerprint,native_fingerprint,contexts in STAGE_SOURCE_OBJECTS:

        native='st5.bin'

        for chapter,row,mode in contexts:

            _,pc_objects,pc_original,native_sections,native_objects,_=stage_tables[chapter]

            if struct.unpack('<HH',exe.bytes(pc_objects+row*4,4))!=(pc_id,mode)or struct.unpack('<HH',original(native_objects+row*4,4))!=(native_id,mode):raise ValueError('Original stage object selection differs')

            expected_original=6437 if(chapter,row)==(6,5)else pc_id

            if struct.unpack('<HH',exe.bytes(pc_original+row*4,4))!=(expected_original,mode):raise ValueError('Original mode stage selection differs')

        if len(pc_model_ids.get(pc,[]))<=part or pc_model_ids[pc][part]!=pc_id or struct.unpack('<H',original(0x0C104B14+npart*2,2))[0]!=native_id:raise ValueError(f'Stage source model identity differs: {pc_id}')

        if pc not in semantic_pc:semantic_pc[pc]=models((args.pc_game/'pol'/pc).read_bytes())[0]

        p=semantic_pc[pc][part];source=semantic_native[native][npart]

        if bounds_fingerprint(p)!=pc_fingerprint or bounds_fingerprint(source)!=native_fingerprint or not supported_model(source,native,nd):raise ValueError(f'Original stage source geometry differs: {pc_id}')

        for key,value in list(meshmap.items()):

            if key[:2]==(pc,part)and value[:2]!=(native,npart):stale_stage_meshes.append(dict(bank=pc,part=part,mesh=key[2],oldNativeBank=value[0],oldNativePart=value[1]));del meshmap[key]

        modelmap[pc,part]=(native,npart,13);modelMaterialIndices[pc,part]=[((m.material[9]&255)^128)-128 for m in source['meshes']]

        whole_stage_sources[pc,part]=(native,npart)

        whole_stage_bindings.append(dict(bank=pc,part=part,pcId=pc_id,nativeBank=native,nativePart=npart,nativeId=native_id,pcVertices=len(p['v']),nativeVertices=len(source['v']),pcMeshes=len(p['meshes']),nativeMeshes=len(source['meshes']),boundsDelta=max(delta(p['v'][:,:3].min(0),source['v'][:,:3].min(0)),delta(p['v'][:,:3].max(0),source['v'][:,:3].max(0))),sphereDelta=delta(np.asarray(p['header']['sphere']),np.asarray(source['header']['sphere'])),contexts=[dict(chapter=c,row=r,mode=m)for c,r,m in contexts]))

    if len(stale_stage_meshes)!=23 or any((r['bank'],r['part'],r['oldNativeBank'],r['oldNativePart'])!=('st6_01.bin',8,'st5.bin',67)for r in stale_stage_meshes):raise ValueError('Unexpected stale lower-authority stage source')

    for key,contexts in list(higher_contexts.items()):

        pc,_,native,_=key

        higher_contexts[key]=[c for c in contexts if(pc,c[0])not in whole_stage_sources or whole_stage_sources[pc,c[0]]==(native,c[2])]

        if not higher_contexts[key]:del higher_contexts[key]

    # Whole original object6306 restores the extra3D frames with their own UVs;

    # its old flat-card texture must never be aliased onto this different art.

    for native_mesh in(38,39,40):

        if semantic_native['st5.bin'][28]['meshes'][native_mesh].material[8]!=96:raise ValueError('Original3D frame material differs')

        higher_contexts['st5_02b.bin',0,'st5.bin',96].append((7,52,28,native_mesh))

    # Chapters1-4 split or merge authored static objects differently. Original
    # source roles plus complete spatial/vertex provenance establish these groups;
    # section indices/row ordinals are never paired merely by position.
    spatial_tables={}
    for chapter,pc_sections,pc_objects,pc_original,pc_size,native_sections,native_objects,native_size,pc_sha,native_sha in SPATIAL_STAGE_TABLES:
        if hashlib.sha256(exe.bytes(pc_sections,pc_size)).hexdigest()!=pc_sha or hashlib.sha256(original(native_sections,native_size)).hexdigest()!=native_sha:raise ValueError('Original spatial-stage section data differs')
        spatial_tables[chapter]=(pc_sections,pc_objects,pc_original,pc_size,native_sections,native_objects,native_size)
    if struct.unpack('<I',original(0x0C104D94+151*4,4))[0]!=0x0C104986 or struct.unpack('<H',original(0x0C1050A0+151*2,2))[0]!=197:raise ValueError('Original merged-stage source list differs')
    spatial_stage_bindings=[];spatial_sources={};stale_spatial_meshes=[]
    for pc_id,pc,part,npart,native_id,proof,pc_fingerprint,native_fingerprint,contexts in SPATIAL_STAGE_OBJECTS:
        native='st1_1.bin'
        if len(pc_model_ids.get(pc,[]))<=part or pc_model_ids[pc][part]!=pc_id or struct.unpack('<H',original(0x0C104986+npart*2,2))[0]!=native_id:raise ValueError('Spatial stage source identity differs')
        for chapter,pc_row,mode,native_rows in contexts:
            _,pc_objects,pc_original,_,_,native_objects,_=spatial_tables[chapter]
            if struct.unpack('<hh',exe.bytes(pc_objects+pc_row*4,4))!=(pc_id,mode)or struct.unpack('<hh',exe.bytes(pc_original+pc_row*4,4))!=(pc_id,mode):raise ValueError('Spatial stage PC/Original role differs')
            if not native_rows or any(struct.unpack('<hh',original(native_objects+row*4,4))!=(native_id,mode)for row in native_rows):raise ValueError('Spatial stage original source role differs')
        if pc not in semantic_pc:semantic_pc[pc]=models((args.pc_game/'pol'/pc).read_bytes())[0]
        p=semantic_pc[pc][part];source=semantic_native[native][npart]
        if bounds_fingerprint(p)!=pc_fingerprint or bounds_fingerprint(source)!=native_fingerprint or not supported_model(source,native,nd):raise ValueError('Spatial stage source geometry differs')
        allowed={(native,npart)}
        if pc_id==6180:allowed.add((native,86))
        for key,value in list(meshmap.items()):
            if key[:2]==(pc,part)and value[:2]not in allowed:stale_spatial_meshes.append(dict(bank=pc,part=part,mesh=key[2],oldNativeBank=value[0],oldNativePart=value[1]));del meshmap[key]
        modelmap[pc,part]=(native,npart,proof);modelMaterialIndices[pc,part]=[((m.material[9]&255)^128)-128 for m in source['meshes']]
        spatial_sources[pc,part]=allowed
        spatial_stage_bindings.append(dict(pcId=pc_id,bank=pc,part=part,nativeBank=native,nativePart=npart,nativeId=native_id,evidence=proof,pcVertices=len(p['v']),nativeVertices=len(source['v']),contexts=[dict(chapter=c,pcRow=r,mode=m,nativeRows=list(nr))for c,r,m,nr in contexts]))
    expected_stale={('st4_06.bin',4,155):18,('st4_06.bin',7,157):1,('st2_07.bin',4,120):6}
    if Counter((r['bank'],r['part'],r['oldNativePart'])for r in stale_spatial_meshes)!=expected_stale or any(r['oldNativeBank']!='st1_1.bin'for r in stale_spatial_meshes):raise ValueError('Unexpected stale spatial-stage correspondence')
    for key,contexts in list(higher_contexts.items()):
        pc,_,native,_=key
        higher_contexts[key]=[c for c in contexts if(pc,c[0])not in spatial_sources or(native,c[2])in spatial_sources[pc,c[0]]]
        if not higher_contexts[key]:del higher_contexts[key]
    # One late PC merged object submits BOTH original sources. A reserved source
    # record avoids fake game IDs or a larger partial-mesh graft. Scene expands
    # it independently and de-duplicates matching evidence14 source identities.
    extra_key='__naomi_stage_6180__.bin';modelmap[extra_key,0]=('st1_1.bin',86,14)
    modelMaterialIndices[extra_key,0]=[((m.material[9]&255)^128)-128 for m in semantic_native['st1_1.bin'][86]['meshes']]
    spatial_stage_extras=[dict(pcId=6180,ordinal=0,nativeBank='st1_1.bin',nativePart=86,nativeId=6710,evidence=14)]
    # Verify the two complete split unions; the actual source headers retain
    # their original whole-object culling spheres.
    for members,npart,tolerance in [((6147,6148),96,.000001),((6156,6157),103,.0001)]:
        parts=[semantic_pc[pc][part]['v'][:,:3]for mid,pc,part,*_ in SPATIAL_STAGE_OBJECTS if mid in members];vertices=np.concatenate(parts);source=semantic_native['st1_1.bin'][npart]['v'][:,:3]
        if max(delta(vertices.min(0),source.min(0)),delta(vertices.max(0),source.max(0)))>tolerance:raise ValueError('Original split-stage spatial union differs')
    for chapter in(1,2):
        sec,obj,_,size,_,_,_=spatial_tables[chapter];data=exe.bytes(sec,size);groups=[tuple(x for x in struct.unpack_from('<12h',data,i)if x>=0)for i in range(0,size,24)]
        active={mid:{i for i,rows in enumerate(groups)for row in rows if struct.unpack('<hh',exe.bytes(obj+row*4,4))==(mid,0)}for mid in(6147,6148)}
        if not active[6147]or active[6147]!=active[6148]:raise ValueError('Split floor/building source members are not co-active')
    # Original kind4A attachment0 (RAMCBF33F4, both first/church states) targets
    # native753/boss1z[2]. Its VTXheader and all twelve reference records plus
    #24 selector bytes are bit-identical to PC674;42/27 source records differ
    # only by <=2.39e-7 position /5.97e-8 normal rounding. Both use bone tuple
    #(1,1,9,0,9), four matrices, and the normal writer without negation.
    # Only this exact ordered layout permits the original native UV atlas.
    if exe.u32(0x0052DE08)!=674 or exe.u32(0x0052DE0C)!=0x00517B80 or[exe.u32(0x0052DE10+i*12)for i in range(4)]!=[42,0,27,0]:raise ValueError('Verified PC skin definition differs')
    for address,size,digest in[(0x00517B80,24,'af71eaee25d99e7c63ccd21905e9ce7f4f3a359e0a4c5254564b2e57ed432e99'),(0x00517C88,240,'02adeee0a7add464c89572eefb1dc83ec95d0f0d091c3a553dd2394c006130d9'),(0x00517D78,1008,'fa4281646cd86ca556b3850e61c98378464942d6c791feba19198d783dba36e6'),(0x00518168,648,'cbe81d25ceac77612573fd103604feb42a8930ec93343d1ca761d6f530059b12')]:
        if hashlib.sha256(exe.bytes(address,size)).hexdigest()!=digest:raise ValueError('Verified PC skin records/reference layout differ')
    if exe.bytes(0x0052DDF0,24)!=bytes.fromhex('1112ffffffff1d1e2223ffffffff05060708ffffffff1213')or exe.bytes(0x004ED1E0,20)!=original(0x0C10E308,20)or struct.unpack('<5I',original(0x0C10E308,20))!=(1,1,9,0,9):raise ValueError('Verified original skin selector/bone groups differ')
    if struct.unpack('<I',original(0x0C10E944+0x4A*4,4))[0]!=0x0C10E8D8 or struct.unpack('<I',original(0x0C10E8D8,4))[0]!=0x0C0A1CA0:raise ValueError('Original skin driver selection differs')
    pc_skin=semantic_pc['char_adv04.bin'][32];native_skin=semantic_native['boss1z.bin'][2]
    if pc_skin['top']!=native_skin['top']or len(pc_skin['meshes'])!=1 or len(pc_skin['v'])!=16 or delta(pc_skin['v'][:,:3],native_skin['v'][:,:3])>1e-6 or not supported_model(native_skin,'boss1z.bin',nd):raise ValueError('Original authored-UV skin layout differs')
    meshmap['char_adv04.bin',32,0]=('boss1z.bin',2,0,16)
    pc_tex=pc_skin['meshes'][0].material[8];native_tex=native_skin['meshes'][0].material[8]
    if nd['boss1z.bin'][native_tex]['w']*nd['boss1z.bin'][native_tex]['h']>pd['char_adv04.bin'][pc_tex]['w']*pd['char_adv04.bin'][pc_tex]['h']:higher_contexts['char_adv04.bin',pc_tex,'boss1z.bin',native_tex].append((32,0,2,0))
    verified_skin_uv=[dict(pcId=674,pcBank='char_adv04.bin',pcPart=32,pcMesh=0,nativeBank='boss1z.bin',nativePart=2,nativeMesh=0,nativeId=753,evidence=16,vertices=16,sourceDefinition=0x0CBF33F4,sourceRecords=[42,0,27,0],normalNegation=False,maxPositionDelta=delta(pc_skin['v'][:,:3],native_skin['v'][:,:3]),maxUvDelta=delta(pc_skin['v'][:,6:],native_skin['v'][:,6:]))]
    # Original Actor22 constructors select PC45/Native4A; their sixteen-node
    # hierarchies/parents/flags and local offsets match (max2.39e-7). Original
    # node callback0C0AE344 submits the cached model; every default source ID is
    # PC+79. These four are rigid submissions, so preserve the entire authored
    # source including its changed UVs or fuller limb anatomy.
    if exe.s16(0x004E0724+0x45*2)!=16:raise ValueError('Original Judgment rig node count differs')
    rig_nodes={}
    def read_judgment_node(address,depth=0):
        if depth>32 or not exe.lo<=address<exe.hi:raise ValueError('Invalid original Judgment rig')
        model,_,_,_,_,index,count=struct.unpack('<I3fIhh',exe.bytes(address,24));rig_nodes[index]=model
        if not 0<=count<=16:raise ValueError('Original Judgment child count differs')
        for child in range(count):read_judgment_node(exe.u32(address+24+child*4),depth+1)
    read_judgment_node(exe.u32(0x004E0430+0x45*4))
    if len(rig_nodes)!=16 or struct.unpack('<I',original(0x0C104D94+131*4,4))[0]!=0x0C103DEC or struct.unpack('<H',original(0x0C1050A0+131*2,2))[0]!=34:raise ValueError('Original Judgment source rig/list differs')
    judgment_rigid_bindings=[]
    for pc_id,part,npart,native_id,bone,pc_hash,native_hash in JUDGMENT_RIGID_SOURCES:
        p=semantic_pc['char_adv04.bin'][part];source=semantic_native['boss1z.bin'][npart]
        if rig_nodes.get(bone)!=pc_id or pc_model_ids['char_adv04.bin'][part]!=pc_id or struct.unpack('<H',original(0x0C103DEC+npart*2,2))[0]!=native_id or native_id!=pc_id+79:raise ValueError('Original Judgment bone model role differs')
        if pol.model_digest(p['header'],p['meshes'])!=pc_hash or pol.model_digest(source['header'],source['meshes'])!=native_hash or p['top']!=source['top']or not supported_model(source,'boss1z.bin',nd):raise ValueError('Original Judgment rigid geometry differs')
        modelmap['char_adv04.bin',part]=('boss1z.bin',npart,9);modelMaterialIndices['char_adv04.bin',part]=[((m.material[9]&255)^128)-128 for m in source['meshes']]
        # No global image alias: the eyes/head attachments use a different
        # original UV atlas. Full rigid geometry retains its source namespace.
        judgment_rigid_bindings.append(dict(pcId=pc_id,pcPart=part,nativeBank='boss1z.bin',nativePart=npart,nativeId=native_id,bone=bone,evidence=9,vertices=len(source['v']),maxPositionDelta=delta(p['v'][:,:3],source['v'][:,:3]),maxUvDelta=delta(p['v'][:,6:],source['v'][:,6:])))
    # Initialized native kind50 in both retained first/church original states
    # selects model935 at node15,1100 at node11 and1592 at node4. In particular
    # the body is native part0, NOT the same-shape alternate-filter part122.
    # Original PCkind4B has the same indexed local hierarchy. For phase1 sources
    # the entire ordered stream uniquely identifies the authored forty-frame
    # family member; never choose a mesh by the PC sampler or filename ordinal.
    if 'boss5.bin'not in semantic_native:semantic_native['boss5.bin']=models(npol['boss5.bin'])[0]
    magician_nodes={}
    def read_magician_node(address,parent=-1,depth=0):
        if depth>32 or not exe.lo<=address<exe.hi:raise ValueError('Invalid original Magician rig')
        model,x,y,z,flags,index,count=struct.unpack('<I3fIhh',exe.bytes(address,24));magician_nodes[index]=(model,parent,flags,x,y,z)
        if not 0<=count<=16:raise ValueError('Original Magician child count differs')
        for child in range(count):read_magician_node(exe.u32(address+24+child*4),index,depth+1)
    read_magician_node(exe.u32(0x004E0430+0x4B*4))
    if exe.s16(0x004E0724+0x4B*2)!=16 or len(magician_nodes)!=16 or[magician_nodes[i][0]for i in range(16)]!=[0,1631,1343,1464,1755,1673,1384,1714,1672,1505,1590,1263,1179,1510,1222,1098]:raise ValueError('Original Magician node roles differ')
    magician_list=struct.unpack('<I',original(0x0C104D94+137*4,4))[0]
    if struct.unpack('<H',original(0x0C1050A0+137*2,2))[0]!=738:raise ValueError('Original Magician source list differs')
    magician_rigid_bindings=[]
    for pc,part,npart,bone,pc_base,native_base,phase,pc_hash,native_hash in MAGICIAN_RIGID_SOURCES:
        if pc not in semantic_pc:semantic_pc[pc]=models((args.pc_game/'pol'/pc).read_bytes())[0]
        p=semantic_pc[pc][part];source=semantic_native['boss5.bin'][npart]
        pc_id=pc_model_ids[pc][part];native_id=struct.unpack('<H',original(magician_list+npart*2,2))[0]
        if magician_nodes[bone][0]!=pc_base or pc_id!=pc_base+phase or native_id!=native_base+phase:raise ValueError('Original Magician source bone/frame role differs')
        if pol.model_digest(p['header'],p['meshes'])!=pc_hash or pol.model_digest(source['header'],source['meshes'])!=native_hash or p['top']!=source['top']or not supported_model(source,'boss5.bin',nd):raise ValueError('Verified original Magician geometry differs')
        if max(delta(p['v'][:,a:b],source['v'][:,a:b])for a,b in((0,3),(3,6),(6,8)))>1e-6 or any(tuple(ctrl for ctrl,_,_ in a.strips)!=tuple(ctrl for ctrl,_,_ in b.strips)for a,b in zip(p['meshes'],source['meshes'])):raise ValueError('Original Magician ordered frame/normal stream differs')
        if phase:
            found=[ni for ni,n in semantic_native['boss5.bin'].items()if n['top']==p['top']and max(delta(p['v'][:,a:b],n['v'][:,a:b])for a,b in((0,3),(3,6),(6,8)))<=1e-6]
            if found!=[npart]:raise ValueError('Original Magician animated source is not unique')
        modelmap[pc,part]=('boss5.bin',npart,9);modelMaterialIndices[pc,part]=[((m.material[9]&255)^128)-128 for m in source['meshes']]
        for mi in range(len(source['meshes'])):meshmap[pc,part,mi]=('boss5.bin',npart,mi,9)
        magician_rigid_bindings.append(dict(pcId=pc_id,pcBank=pc,pcPart=part,nativeBank='boss5.bin',nativePart=npart,nativeId=native_id,bone=bone,phase=phase,evidence=9,vertices=len(source['v']),maxPositionDelta=delta(p['v'][:,:3],source['v'][:,:3]),maxNormalDelta=delta(p['v'][:,3:6],source['v'][:,3:6]),maxUvDelta=delta(p['v'][:,6:],source['v'][:,6:])))
    # Front/back Emperor copies share topology/XYZ/UV but carry opposite
    # normals and different reflection materials. Resolve by the COMPLETE signed
    # ordered normal stream, never by filename ordinal or absolute normals.
    if 'boss6.bin'not in semantic_native:semantic_native['boss6.bin']=models(npol['boss6.bin'])[0]
    if 'boss6.bin'not in semantic_pc:semantic_pc['boss6.bin']=models((args.pc_game/'pol/boss6.bin').read_bytes())[0]
    boss6_normal_bindings=[]
    for part,expected_source,pc_hash,native_hash in BOSS6_SIGNED_NORMAL_SOURCES:
        p=semantic_pc['boss6.bin'][part];found=[]
        for ni,source in semantic_native['boss6.bin'].items():
            if p['top']==source['top']and delta(p['v'][:,:3],source['v'][:,:3])<=1e-6 and delta(p['v'][:,3:6],source['v'][:,3:6])<=1e-6 and delta(p['v'][:,6:],source['v'][:,6:])<=1e-6:found.append((ni,source))
        if len(found)!=1 or found[0][0]!=expected_source:raise ValueError('Original signed-normal source is not unique')
        ni,source=found[0]
        if pol.model_digest(p['header'],p['meshes'])!=pc_hash or pol.model_digest(source['header'],source['meshes'])!=native_hash or not supported_model(source,'boss6.bin',nd):raise ValueError('Verified Emperor surface data differs')
        for mi,(a,b)in enumerate(zip(p['meshes'],source['meshes'])):
            if tuple(ctrl for ctrl,_,_ in a.strips)!=tuple(ctrl for ctrl,_,_ in b.strips)or a.material[1:3]!=b.material[1:3]or a.material[9:]!=b.material[9:]or delta(np.asarray([pol.f32(v)for v in a.material[3:8]]),np.asarray([pol.f32(v)for v in b.material[3:8]]))>1e-6 or(a.material[8],b.material[8])not in((1,0),(4,3)):raise ValueError('Original Emperor strip/material context differs')
            meshmap['boss6.bin',part,mi]=('boss6.bin',ni,mi,18)
        boss6_normal_bindings.append(dict(pcPart=part,nativePart=ni,evidence=18,vertices=len(p['v']),maxPositionDelta=delta(p['v'][:,:3],source['v'][:,:3]),maxNormalDelta=delta(p['v'][:,3:6],source['v'][:,3:6]),maxUvDelta=delta(p['v'][:,6:],source['v'][:,6:])))
    # Source-role closure for the remaining actual rigid draws. Native Task40
    # draws D7F+i for i<40 (0770BC..C0), breakable Task0718B0 draws shadow11D3
    # after the original scale8/Y+.2 path (071D30..5C), Bridge6 Task098B90
    # draws1B63 in state6, and OrbSpark033C94 draws19F7+counter for0..4.
    for address,value in((0x0C13C670+40*4,0x0C076BEC),(0x0C13C670+12*4,0x0C07277C)):
        if struct.unpack('<I',original(address,4))[0]!=value:raise ValueError('Original prop task selection differs')
    for address,value in((0x0C0770EC,0xD7F),(0x0C071E38,0x11D3),(0x0C098BEA,0x1B63),(0x0C033D22,0x19F7)):
        if struct.unpack('<H',original(address,2))[0]!=value:raise ValueError('Original rigid effect source ID differs')
    # The TWO burning cars before Judgment are Actor28, not the later parked
    # Actor33 cars. Original constructor table[type28] selects03B730, whose
    # unconditional model125 write and stored-model draw prove RED car_1[8].
    # Its small authored export differences defeated the generic mesh matcher,
    # which incorrectly selected same-shape gray parked car_1[11] surfaces.
    if struct.unpack('<I',original(0x0C13BE18+0x28*4,4))[0]!=0x0C03B730 or original(0x0C03B756,10)!=bytes.fromhex('4c907de3f3650475360e')or original(0x0C03B90C,8)!=bytes.fromhex('759046db0b4b8e04'):raise ValueError('Original Judgment burning-car source callback differs')
    # The other user of PCmodel51 is Actor27 after burning. Both its normal
    # tick190 transition and skip-to190 path likewise select125, so a whole-ID
    # binding is safe rather than needing an actor-specific resource exception.
    if struct.unpack('<I',original(0x0C13BE18+0x27*4,4))[0]!=0x0C03BA60 or any(struct.unpack('<H',original(a,2))[0]!=v for a,v in((0x0C03BA80,0xE375),(0x0C03BAA0,0xE37D),(0x0C03BAF8,0xE17D))):raise ValueError('Original alternate burning-car role differs')
    if struct.unpack('<4H',exe.bytes(0x00589AE0,8))!=(0x145,671,0x146,667)or struct.unpack('<4H',original(0x0C1329A8,8))!=(0x120,671,0x121,667):raise ValueError('Original Judgment burning-car path roles differ')
    # Original SpawnTrail0861F4 creates callback0860E8 and explicitly stores
    # model0542 (boss5[411], driver0), NOT the same-shape0541/full-intensity
    # copy. Lifetime startsFF, decrements6, and the callback draws stored+13EC.
    for address,value in((0x0C0862BC,0x0C0860E8),(0x0C0862C0,0x0C0B83D4),(0x0C0861EC,0x0C09A78E)):
        if struct.unpack('<I',original(address,4))[0]!=value:raise ValueError('Original Magician afterimage callback differs')
    for address,value in((0x0C0862B2,0xFF),(0x0C0862B4,0x131C),(0x0C0862B6,0x542),(0x0C0862B8,0x13EC)):
        if struct.unpack('<H',original(address,2))[0]!=value:raise ValueError('Original Magician afterimage lifetime/model differs')
    for address,code in((0x0C086200,'2fd32ed452950b430900'),(0x0C086292,'0e91260e0d900d92160e0c90260e'),(0x0C0860FA,'fa73360e'),(0x0C0861A6,'11d30b900b43ee04')):
        if original(address,len(code)//2)!=bytes.fromhex(code):raise ValueError('Original Magician afterimage source submission differs')
    # Original kind12 authored records choose the opaque variants explicitly,
    # not by similarity to a PC material. The40-byte records have identical
    # flags, pose, rotation and parameters; only their source model field differs.
    pc_event=(args.pc_game/'evt/st5evtbl.bin').read_bytes()
    static_records=((0x3060,0x1F8430,6300,6871),(0x3088,0x1F8458,6302,6873),(0x3038,0x1F8408,6304,6875))
    for po,no,pc_id,native_id in static_records:
        a=pc_event[po:po+40];b=program[no:no+40]
        if len(a)!=40 or a[:34]!=b[:34]or a[36:]!=b[36:]or struct.unpack_from('<I',a)[0]!=65 or a[37]!=12 or struct.unpack_from('<H',a,34)[0]!=pc_id or struct.unpack_from('<H',b,34)[0]!=native_id:raise ValueError('Original static prop authored record differs')
    source_lists={'komono_7.bin':41,'common.bin':1,'garasu.bin':26,'st5.bin':154,'komono_st6.bin':47,'eff_2.bin':9,'hito_buru.bin':103,'boss1q.bin':130,'boss5.bin':137,'car_1.bin':21}
    def native_model_ids(bank):
        fi=source_lists[bank];at=struct.unpack('<I',original(0x0C104D94+fi*4,4))[0];count=struct.unpack('<H',original(0x0C1050A0+fi*2,2))[0]
        return struct.unpack('<'+'H'*count,original(at,count*2))
    def model_group_digest(ms,parts):return hashlib.sha256(b''.join(bytes.fromhex(pol.model_digest(ms[i]['header'],ms[i]['meshes']))for i in parts)).hexdigest()
    def normal_ulp(a,b):
        def ordered(v):
            bits=np.ascontiguousarray(v,dtype=np.float32).view(np.uint32).astype(np.int64)
            return np.where(bits&0x80000000,0xFFFFFFFF-bits,bits|0x80000000)
        return int(np.max(np.abs(ordered(a)-ordered(b))))
    expected_source_ids={'boat_normals':([4199,4204,4206],[4506,4511,4513]),'judgment_burning_cars':([51],[125]),'boat_shadow':([4305],[4563]),'glass_fragments':(list(range(3237,3277)),list(range(3455,3495))),'bridge6':([6420],[7011]),'static_kind12':([6300,6302,6304],[6871,6873,6875]),'identical_st6_props':([6328,6332,6333],[6919,6922,6919]),'orb_spark':(list(range(6064,6069)),list(range(6647,6652))),'magician_afterimage':([1509],[1346]),'civilian_limbs':([3689,3690],[3941,3942])}
    live_rigid_bindings=[];stale_burning_car_meshes=[]
    for group,pc,native,pairs,proof,pc_hash,native_hash in LIVE_RIGID_SOURCE_GROUPS:
        if pc not in semantic_pc:semantic_pc[pc]=models((args.pc_game/'pol'/pc).read_bytes())[0]
        if native not in semantic_native:semantic_native[native]=models(npol[native])[0]
        pm=semantic_pc[pc];nm=semantic_native[native];native_ids=native_model_ids(native)
        if model_group_digest(pm,[p for p,_ in pairs])!=pc_hash or model_group_digest(nm,[n for _,n in pairs])!=native_hash:raise ValueError('Verified live rigid source data differs: '+group)
        if [pc_model_ids[pc][p]for p,_ in pairs]!=expected_source_ids[group][0]or[native_ids[n]for _,n in pairs]!=expected_source_ids[group][1]:raise ValueError('Verified live rigid resource roles differ: '+group)
        if group=='identical_st6_props':
            for a,b in((68,73),(71,72)):
                if pol.model_digest(nm[a]['header'],nm[a]['meshes'])!=pol.model_digest(nm[b]['header'],nm[b]['meshes']):raise ValueError('Original duplicated prop visuals differ')
        for part,npart in pairs:
            p=pm[part];source=nm[npart]
            # Only the explicitly authored static card and original civilian
            # export have larger deltas. No generic correspondence gate changes.
            limits=(.018,.009,.0007)if group=='judgment_burning_cars'else(.43,.00003,1e-5)if group=='static_kind12'else(.001,.0006,.001)if group=='civilian_limbs'else(1e-5,4e-6,1e-5)
            if p['top']!=source['top']or any(delta(p['v'][:,a:b],source['v'][:,a:b])>lim for(a,b),lim in zip(((0,3),(3,6),(6,8)),limits))or not supported_model(source,native,nd):raise ValueError('Verified live rigid ordered geometry differs: '+group)
            if any(tuple(c for c,_,_ in a.strips)!=tuple(c for c,_,_ in b.strips)for a,b in zip(p['meshes'],source['meshes'])):raise ValueError('Verified live rigid strip controls differ: '+group)
            if group=='boat_normals':
                found=[]
                for ni,q in nm.items():
                    if q['top']==p['top']and delta(p['v'][:,:3],q['v'][:,:3])<=1e-6 and delta(p['v'][:,6:],q['v'][:,6:])<=1e-5 and normal_ulp(p['v'][:,3:6],q['v'][:,3:6])<=1:found.append(ni)
                if found!=[npart]:raise ValueError('Boat complete signed-normal source is not unique within1ULP')
            modelmap[pc,part]=(native,npart,proof);modelMaterialIndices[pc,part]=[((m.material[9]&255)^128)-128 for m in source['meshes']]
            if group=='judgment_burning_cars':
                for key,value in list(meshmap.items()):
                    if key[:2]==(pc,part)and value[:2]!=(native,npart):stale_burning_car_meshes.append(dict(pcBank=pc,pcPart=part,pcMesh=key[2],oldNativeBank=value[0],oldNativePart=value[1],oldNativeMesh=value[2]));del meshmap[key]
                for key,contexts in list(higher_contexts.items()):
                    if key[0]!=pc:continue
                    higher_contexts[key]=[c for c in contexts if c[0]!=part or(key[2],c[2])==(native,npart)]
                    if not higher_contexts[key]:del higher_contexts[key]
                for mi,(a,b)in enumerate(zip(p['meshes'],source['meshes'])):
                    meshmap[pc,part,mi]=(native,npart,mi,proof)
                    pt,nt=a.material[8],b.material[8]
                    if pt!=0xFFFFFFFF and nt!=0xFFFFFFFF and nd[native][nt]['w']*nd[native][nt]['h']>pd[pc][pt]['w']*pd[pc][pt]['h']:higher_contexts[pc,pt,native,nt].append((part,mi,npart,mi))
            live_rigid_bindings.append(dict(group=group,pcId=pc_model_ids[pc][part],pcBank=pc,pcPart=part,nativeBank=native,nativePart=npart,nativeId=native_ids[npart],evidence=proof,vertices=len(source['v']),maxPositionDelta=delta(p['v'][:,:3],source['v'][:,:3]),maxNormalDelta=delta(p['v'][:,3:6],source['v'][:,3:6]),maxUvDelta=delta(p['v'][:,6:],source['v'][:,6:])))
    # PC668 occurs ONLY at nodes2/8 of PCkind44 across all86 PC rigs. Original
    # Nativekind49 has737 at node2 but747 at node8, so there is deliberately NO
    # global668 replacement. Scene restricts this reserved role to rigid Mount
    # task48FE90/node8. Both original indexed rigs/flags/parents/local frames
    # match, maxoffset delta4.77e-7; constructor hides node2.
    mount_uses=[]
    for kind in range((0x004E07D0-0x004E0724)//2):
        seen=set()
        def mount_use(address):
            if address in seen:return
            if not exe.lo<=address<exe.hi:raise ValueError('Invalid original skeleton source')
            seen.add(address);model,_,_,_,_,index,count=struct.unpack('<I3fIhh',exe.bytes(address,24))
            if model==668:mount_uses.append((kind,index))
            if not 0<=count<60:raise ValueError('Original skeleton child count differs')
            for ci in range(count):mount_use(exe.u32(address+24+ci*4))
        mount_use(exe.u32(0x004E0430+kind*4))
    if sorted(mount_uses)!=[(0x44,2),(0x44,8)]:raise ValueError('Reused original Mount model has an additional role')
    if 'boss1q.bin'not in semantic_native:semantic_native['boss1q.bin']=models(npol['boss1q.bin'])[0]
    p=semantic_pc['char_adv04.bin'][26];source=semantic_native['boss1q.bin'][14]
    if pol.model_digest(p['header'],p['meshes'])!=MOUNT_NODE_SOURCE[0]or pol.model_digest(source['header'],source['meshes'])!=MOUNT_NODE_SOURCE[1]or native_model_ids('boss1q.bin')[14]!=747 or p['top']!=source['top']or max(delta(p['v'][:,a:b],source['v'][:,a:b])for a,b in((0,3),(3,6),(6,8)))>1e-6:raise ValueError('Original Mount node8 source differs')
    role='__naomi_node_668__.bin';modelmap[role,8]=('boss1q.bin',14,19);modelMaterialIndices[role,8]=[((m.material[9]&255)^128)-128 for m in source['meshes']]
    if('char_adv04.bin',26)in modelmap:raise ValueError('Reused Mount model was globally substituted')
    mount_role_bindings=[dict(pcId=668,node=8,pcKind=0x44,nativeKind=0x49,nativeBank='boss1q.bin',nativePart=14,nativeId=747,evidence=19)]
    # The PC church is a merged/reordered subset of native stage part102.

    # Transfer only texture bindings proved by unique per-mesh geometry AND UV

    # correspondence; do not replace the PC part with the larger native model.

    nm,_=models(npol['st1_1.bin']);pm,_=models((args.pc_game/'pol/st1_1.bin').read_bytes())

    church_matches=0

    for pi,p in enumerate(pm[32]['meshes']):

        pv=np.asarray([v for _,_,vs in p.strips for v in vs],dtype=np.float32).reshape(-1,8)

        pt=tuple((k,len(vs))for _,k,vs in p.strips);found=[]

        for ni,n in nm.items():

            for j,m in enumerate(n['meshes']):

                if tuple((k,len(vs))for _,k,vs in m.strips)!=pt:continue

                nv=np.asarray([v for _,_,vs in m.strips for v in vs],dtype=np.float32).reshape(-1,8)

                if delta(nv[:,:3],pv[:,:3])<=.001 and delta(nv[:,6:],pv[:,6:])<=1e-5:found.append((ni,j,m))

        if len(found)!=1:continue

        ni,j,m=found[0];pii=p.material[8];nii=m.material[8]

        meshmap['st1_1.bin',32,pi]=('st1_1.bin',ni,j,1)

        if pii!=0xFFFFFFFF and nii!=0xFFFFFFFF and nii<len(nd['st1_1.bin'])and supported(nd['st1_1.bin'][nii]):

            aliases['st1_1.bin',pii].add(('st1_1.bin',nii));church_matches+=1

    # The visible opening church front is in a split PC runtime bank. Its gable

    # and rose-window details correspond to specific meshes in the merged native

    # stage. The gable's PC UV rounding differs by at most 0.000791, not a new

    # atlas layout. Verify both position and UV streams before binding its art.

    front,_=models((args.pc_game/'pol/st1_01c.bin').read_bytes())

    for pi,ni in {66:134,67:135,205:391}.items():

        p=front[0]['meshes'][pi];m=nm[95]['meshes'][ni]

        pv=np.asarray([v for _,_,vs in p.strips for v in vs],dtype=np.float32).reshape(-1,8)

        nv=np.asarray([v for _,_,vs in m.strips for v in vs],dtype=np.float32).reshape(-1,8)

        if tuple((k,len(vs))for _,k,vs in p.strips)!=tuple((k,len(vs))for _,k,vs in m.strips)or delta(pv[:,:3],nv[:,:3])>.001 or delta(pv[:,6:],nv[:,6:])>.001 or not supported(nd['st1_1.bin'][m.material[8]]):

            raise ValueError(f'Verified opening church binding differs: PCmesh{pi}/Naomimesh{ni}')

        aliases['st1_01c.bin',p.material[8]].add(('st1_1.bin',m.material[8]))

        meshmap['st1_01c.bin',0,pi]=('st1_1.bin',95,ni,3)

    # Semantic material correspondences independently verified from the actual

    # muzzle/blood model families, including animation frames with repeated quads.

    manual={('common.bin',i):('common.bin',i-1)for i in range(1,26)}

    manual.update({('common.bin',i):('common.bin',i+16)for i in range(26,34)})

    manual.update({('common.bin',36):('common.bin',53),('common.bin',71):('common.bin',91),('common.bin',40):('common.bin',40),('common.bin',43):('common.bin',59),('common.bin',44):('common.bin',60)})

    for key,value in manual.items():

        if key[1]<len(pd.get(key[0],[]))and value[1]<len(nd.get(value[0],[]))and supported(nd[value[0]][value[1]]):aliases[key]={value}

    # Exact decoded image matches cover more unchanged art while retaining blood

    # membership. They cannot transfer a blood recolor into unrelated white art.

    n_blood={x['id']for x in nd['scr_blood_red.bin']};p_blood={x['id']for x in pd['scr_blood_red.bin']}

    image_matches=0

    pc_palettes=tex.load_palette_banks(exe)

    # All73 English glyphs keep their exact opaque footprint. Verify each glyph

    # against both PC runtime font variants before sharing the original native

    # 8-bit glyph and96-color palette; the native antialiasing remains intact.

    native_font,_=unpack(ntex['scr_jimaku_e.bin']);english_mappings=0

    for font,pc_palette in [('scr_jimaku_e.bin',0),('scr_jimaku_e2.bin',26)]:

        pc_font,_=unpack((args.pc_game/'tex'/font).read_bytes())

        if len(pd[font])!=len(nd['scr_jimaku_e.bin']):raise ValueError('Unexpected English font glyph count')

        for p,d in zip(pd[font],nd['scr_jimaku_e.bin']):

            nw,nh,pixels=native_pixels(native_font,d,palettes);pw,ph,pv=tex.decode_texture(pc_font,p,tex.palette_for(pc_palettes,pc_palette,p['fmt']&7));pc_pixels=tex.rgba_bytes(pv,p['fmt']&7)

            if(nw,nh)!=(pw,ph)or any((a!=0)!=(b!=0)for a,b in zip(pixels[3::4],pc_pixels[3::4])):raise ValueError(f'English glyph identity differs: {font}/{p["index"]}')

            aliases[font,p['index']]={('scr_jimaku_e.bin',d['index'])};english_mappings+=1

    native_images=defaultdict(list);pc_images={}

    for name,ns in nd.items():

        native,_=unpack(ntex[name]);nh=defaultdict(list)

        for d in ns:

            if not supported(d):continue

            try:w,h,pixels=native_pixels(native,d,palettes);sha=hashlib.sha256(pixels).digest()

            except (ValueError,IndexError):continue

            identity=w,h,sha,d['id']in n_blood

            nh[identity].append(d['index']);native_images[identity].append((name,d['index']))

        ps=pd.get(name,[]);pfile=args.pc_game/'tex'/name

        if not ps or not pfile.exists():continue

        pc,_=unpack(pfile.read_bytes())

        for d in ps:

            if(name,d['index'])in manual or not supported(d):continue

            if name in('scr_jimaku_e.bin','scr_jimaku_e2.bin'):continue

            palette=tex.palette_for(pc_palettes,1,d['fmt']&7)if((d['fmt']>>8)&255)==5 and name.startswith('scr_jimaku')else None

            try:w,h,v=tex.decode_texture(pc,d,palette);sha=hashlib.sha256(tex.rgba_bytes(v,d['fmt']&7)).digest()

            except (ValueError,IndexError):continue

            identity=w,h,sha,d['id']in p_blood;pc_images[name,d['index']]=identity

            found=nh.get(identity,[])

            if len(found)==1:aliases[name,d['index']].add((name,found[0]));image_matches+=1

    # Split PC banks can retain byte-identical artwork under a different native

    # bank name. An exact decoded image plus dimensions proves an artwork alias

    # even when geometry was regrouped. Retain every authored/contextual binding,

    # and require original blood membership on both sides of the identity key.

    global_image_candidates={}

    for name,ps in pd.items():

        pending=[d for d in ps if not aliases[name,d['index']]and supported(d)]

        if not pending or name in('scr_jimaku_e.bin','scr_jimaku_e2.bin')or not(args.pc_game/'tex'/name).exists():continue

        pc=None

        for d in pending:

            key=name,d['index'];identity=pc_images.get(key)

            if identity is None:

                if pc is None:pc,_=unpack((args.pc_game/'tex'/name).read_bytes())

                palette=tex.palette_for(pc_palettes,1,d['fmt']&7)if((d['fmt']>>8)&255)==5 and name.startswith('scr_jimaku')else None

                try:w,h,v=tex.decode_texture(pc,d,palette);sha=hashlib.sha256(tex.rgba_bytes(v,d['fmt']&7)).digest()

                except (ValueError,IndexError):continue

                identity=w,h,sha,d['id']in p_blood

            found=native_images.get(identity,[])

            # Equal red pixels can have different corresponding green-bank

            # entries; leave blood frames to the original semantic bindings.

            if found and not identity[3]:global_image_candidates[key]=min(found)

    # Unmatched world meshes often regroup the same original triangles into

    # different strip/material batches. Prove artwork identity from individual

    # position+UV triangles without claiming a whole native mesh substitution.

    # A single native artwork identity must explain every matched fragment of

    # that PC texture; inconsistent atlas candidates remain unresolved.

    identical_art_aliases=0

    for key,refs in aliases.items():

        if len(refs)>1 and len({native_texture_identity(*ref)for ref in refs})==1:aliases[key]={sorted(refs)[0]};identical_art_aliases+=1

    pending=[row for row in stage_fragments if not aliases[row[0],row[1]]]

    triangle_aliases=[]

    if pending:

        triangle_index=defaultdict(list);environment_banks={'st1_1.bin','st3.bin','st4.bin','st5.bin'}

        def triangle_cell(t):

            mean=t.mean(axis=0);return tuple(math.floor(float(x))for x in mean[:3])+tuple(math.floor(float(x)*100)for x in mean[3:])

        for(top,_),records in environment_index.items():

            for native,part,mesh,material,v in records:

                if native not in environment_banks:continue

                for triangle in triangle_stream(top,v):triangle_index[triangle_cell(triangle)].append((native,material[8],triangle))

        permutations=np.asarray(list(itertools.permutations(range(3))));possible={};hits=Counter();representatives={}

        for pc,pc_texture,top,v in pending:

            key=pc,pc_texture

            for triangle in triangle_stream(top,v):

                cell=triangle_cell(triangle);identities=set()

                for offset in itertools.product((-1,0,1),repeat=5):

                    neighbor=tuple(x+y for x,y in zip(cell,offset))

                    for native,texture,source in triangle_index.get(neighbor,()):

                        if np.any(np.max(np.abs(source[permutations]-triangle),axis=(1,2))<=.001):

                            identity=native_texture_identity(native,texture);identities.add(identity);representatives.setdefault(identity,(native,texture))

                if identities:

                    possible[key]=identities if key not in possible else possible[key]&identities;hits[key]+=1

        for key,candidates in possible.items():

            if len(candidates)!=1 or None in candidates:continue

            native,texture=representatives[next(iter(candidates))];aliases[key]={(native,texture)}

            p,d=pd[key[0]][key[1]],nd[native][texture]

            triangle_aliases.append(dict(bank=key[0],pcDescriptor=key[1],nativeBank=native,nativeDescriptor=texture,matchedTriangles=hits[key],higherResolution=d['w']*d['h']>p['w']*p['h']))

    # Geometry/UV-proven art, including genuine higher-resolution source art,

    # takes precedence over a byte-identical image reused elsewhere in the ROM.

    global_image_matches=0

    for key,ref in global_image_candidates.items():

        if not aliases[key]:aliases[key]={ref};global_image_matches+=1

    resolved={k:next(iter(v))for k,v in aliases.items()if len(v)==1}

    # All resources and palette colors come from the original native data.

    resources=[]

    for name,raw in sorted(npol.items()):

        if not name.endswith('.bin'):continue

        try:pol.header_count(unpack(raw)[0])

        except (ValueError,IndexError,pol.FormatError):continue

        resources.append((0,name,[],raw))

    for name,ds in sorted(nd.items()):resources.append((1,name,ds,ntex[name]))

    payload=bytearray()

    def word(v):payload.extend(struct.pack('<I',v))

    def string(s):b=s.encode('ascii');word(len(b));payload.extend(b)

    for kind,name,ds,raw in resources:

        decoded,prs=unpack(raw);word(kind);string(name);word(len(ds));word(prs);word(len(decoded));word(len(raw))

        for d in ds:payload.extend(struct.pack('<HHIII',d['w'],d['h'],d['fmt'],d['offset'],d['id']))

        payload.extend(raw)

    word(len(palettes))

    for index,palette in sorted(palettes.items()):

        word(index);word(palette['format']);word(len(palette['colors']));payload.extend(struct.pack('<'+'H'*len(palette['colors']),*palette['colors']))

    palette_bindings=[(name,d['index'],0 if((d['fmt']>>8)&255)==7 else 1)for name,ds in nd.items()for d in ds if((d['fmt']>>8)&255)in(5,7)]

    word(len(palette_bindings))

    for name,index,palette in palette_bindings:

        native,_=unpack(ntex[name]);native_pixels(native,nd[name][index],palettes)

        string(name);word(index);word(palette)

    word(len(meshmap))

    for(pc,part,mesh),(native,npart,nmesh,evidence)in sorted(meshmap.items()):string(pc);word(part);word(mesh);string(native);word(npart);word(nmesh);word(evidence)

    for(pc,pi),(native,ni)in sorted(resolved.items()):string(pc);word(pi);string(native);word(ni)

    for(pc,pi),(native,ni,reason)in sorted(modelmap.items()):string(pc);word(pi);string(native);word(ni);word(reason)

    header=b'H2NVIS01'+struct.pack('<6I',2,len(payload),zlib.crc32(payload)&0xFFFFFFFF,len(resources),len(resolved),len(modelmap))

    args.out.parent.mkdir(parents=True,exist_ok=True);temporary=args.out.with_suffix(args.out.suffix+'.tmp');temporary.write_bytes(header+payload);temporary.replace(args.out)

    reasons={1:'unique_geometry_uv',2:'validated_stable_part_bank',3:'verified_effect_family',4:'original_ripple_callback_family',5:'original_polygon_blood_sequence',6:'original_sky_table_binding',8:'original_cross_bank_callback',9:'original_rigid_skeleton_binding',13:'original_whole_stage_object',14:'original_grouped_stage_source',15:'original_spatial_stage_source',19:'original_node_specific_rigid_source',20:'original_rigid_source_context',21:'unique_signed_normal_stream',22:'identical_native_visual_context'}

    modelReasons=Counter(reasons[v[2]]for v in modelmap.values())

    indices=Counter(x for key in modelmap for x in modelMaterialIndices[key])

    referenced=set()

    for(native,part,_)in modelmap.values():referenced.update((native,part,mi)for mi in range(native_mesh_counts[native,part]))

    referenced.update((native,part,mesh)for native,part,mesh,_ in meshmap.values())

    shading=Counter((native_materials[key][2]>>6)&3 for key in referenced)

    ignore_alpha=Counter(((native_materials[key][2]>>6)&3,(native_materials[key][2]>>19)&1)for key in referenced)

    list_types=Counter(((native_materials[key][0]>>24)&7,(native_materials[key][2]&0x180000)!=0x80000)for key in referenced)

    referenced_indices=Counter(((native_materials[key][9]&255)^128)-128 for key in referenced)

    higher_missing=[dict(bank=name,pcDescriptor=pi,nativeBank=native,naomiDescriptor=ni,pcModel=part,pcMesh=mesh)for(name,pi,native,ni),contexts in sorted(higher_contexts.items())for part,mesh,npart,nmesh in contexts if meshmap.get((name,part,mesh),())[:3]!=(native,npart,nmesh)and modelmap.get((name,part),())[:2]!=(native,npart)and(native,npart)not in spatial_sources.get((name,part),set())]

    report={

        'format':2,'source_program_sha1':PROGRAM_SHA1,'bank':str(args.out.resolve()),

        'bank_bytes':len(header)+len(payload),'bank_sha256':hashlib.sha256(header+payload).hexdigest(),

        'resources':len(resources),'native_texture_banks':len(nd),'native_texture_descriptors':sum(map(len,nd.values())),

        'supported_native_descriptors':sum(supported(d)for ds in nd.values()for d in ds),

        'unsupported_palette_descriptors':0,'native_palette_textures':len(palette_bindings),

        'source_palettes':[dict(index=i,source=p['source'],colors=len(p['colors']),capacity=p['capacity'],format=p['format'])for i,p in sorted(palettes.items())],

        'english_glyph_mappings':english_mappings,'texture_mappings':len(resolved),

        'ambiguous_texture_mappings_omitted':sum(len(v)>1 for v in aliases.values()),

        'church_mesh_texture_matches':church_matches,'rigid_model_mappings':sum(not key[0].startswith('__naomi_')for key in modelmap),'model_mappings':len(modelmap),'native_polygon_blood_frames':len(polygon_parts),'mesh_mappings':len(meshmap),

        'native_higher_resolution_pairs':len(higher_contexts),'unmapped_native_higher_resolution_meshes':higher_missing,

        'split_stage_coverage':split_coverage,

        'nonstage_coverage':nonstage_coverage,

        'byte_identical_native_candidates_collapsed':identical_candidates_collapsed,

        'ambiguous_geometry_native_texture_aliases':ambiguous_geometry_texture_aliases,

        'global_exact_native_image_aliases':global_image_matches,

        'verified_rigid_native_uv_bindings':rigid_uv_bindings,

        'verified_rigid_uv_material_transfers':rigid_uv_material_bindings,

        'verified_whole_stage_objects':whole_stage_bindings,

        'removed_stale_stage_mesh_bindings':stale_stage_meshes,
        'verified_emperor_signed_normal_sources':boss6_normal_bindings,
        'verified_magician_rigid_sources':magician_rigid_bindings,
        'verified_live_rigid_sources':live_rigid_bindings,'verified_mount_node_sources':mount_role_bindings,
        'removed_misidentified_burning_car_mesh_bindings':stale_burning_car_meshes,
        'verified_judgment_rigid_sources':judgment_rigid_bindings,
        'verified_authored_skin_uv':verified_skin_uv,
        'verified_spatial_stage_objects':spatial_stage_bindings,'additional_stage_sources':spatial_stage_extras,
        'removed_stale_spatial_mesh_bindings':stale_spatial_meshes,

        'byte_identical_native_art_aliases_collapsed':identical_art_aliases,

        'triangle_native_texture_aliases':triangle_aliases,

        'triangle_native_texture_alias_count':len(triangle_aliases),

        'model_evidence_counts':dict(modelReasons),'mapped_material_signed_byte9':dict(sorted(indices.items())),

        'referenced_native_shading_modes':dict(sorted(shading.items())),

        'referenced_native_ignore_alpha_by_shading':{str(key):value for key,value in sorted(ignore_alpha.items())},

        'referenced_native_list_types_vs_legacy_translucent':{str(key):value for key,value in sorted(list_types.items())},

        'referenced_native_signed_byte9':dict(sorted(referenced_indices.items())),

        'polygon_blood_source_table':0x0C0E0438,'polygon_blood_native_parts':polygon_parts,

        'coverage':coverage,'unsupported_banks':unsupported,

        'limits':['Native mesh transfer preserves CPU record addresses only when source correspondence is verified.',

                  'Flagged alternative model representations still require dedicated decoding where submitted.',

                  'No camera, motion, gameplay, enemy timing or network data is changed.',

                  'Only data resources are stored; no ROM/BIOS/executable is required or executed at runtime.']}

    report_path=args.report or args.out.with_suffix('.json');report_path.write_text(json.dumps(report,indent=2));print(json.dumps({k:v for k,v in report.items()if k not in('coverage','unsupported_banks','limits','split_stage_coverage','nonstage_coverage','polygon_blood_native_parts','triangle_native_texture_aliases')},indent=2),flush=True)



if __name__=='__main__':main()
