/*
     This file is part of GNUnet.
     Copyright (C) 2001, 2002, 2003, 2004, 2006 Christian Grothoff (and other contributing authors)

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 3, or (at your
     option) any later version.

     GNUnet is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with GNUnet; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
     Boston, MA 02110-1301, USA.

     For the actual CRC code:
     Copyright abandoned; this code is in the public domain.
     Provided to GNUnet by peter@horizon.com
*/

/**
 * @file util/test_crypto_crc.c
 * @brief testcase for crypto_crc.c
 */
#include "platform.h"
#include "gnunet_util_lib.h"

static int expected[] = {
  -1223996378, 929797997, -1048047323, 1791081351, -425765913, 2138425902,
  82584863, 1939615314, 1806463044, -1505003452, 1878277636, -997353517,
  201238705, 1723258694, -1107452366, -344562561, -1102247383, 1973035265,
  715213337, -1886586005, 2021214515, -1387332962, 593019378, -571088044,
  1412577760, 412164558, -1626111170, 1556494863, -289796528, -850404775,
  2066714587, -911838105, -1426027382, 499684507, -835420055, 1817119454,
  -1221795958, 1516966784, -1038806877, -2115880691, 532627620, 1984437415,
  -396341583, -1345366324, -590766745, -1801923449, 1752427988, -386896390,
  453906317, 1552589433, -858925718, 1160445643, -740188079, -486609040,
  1102529269, -515846212, -1614217202, 1572162207, 943558923, -467330358,
  -1870764193, 1477005328, -793029208, -888983175, -696956020, 842706021,
  1642390067, -805889494, 1284862057, 1562545388, 2091626273, 1852404553,
  -2076508101, 370903003, 1186422975, 1936085227, 769358463, 180401058,
  2032612572, -105461719, -1119935472, 617249831, 1169304728, 1771205256,
  -2042554284, 653270859, -918610713, 336081663, -913685370, 1962213744,
  -505406126, -838622649, -1141518710, 893143582, -1330296611, 122119483,
  1111564496, 688811976, 1016241049, -1803438473, 359630107, 1034798954,
  -581359286, 1590946527, -389997034, 2020318460, 1695967527, -464069727,
  -862641495, -1405012109, -771244841, 738226150, -1035328134, -933945474,
  1254965774, 1661863830, -884127998, 1800460481, 814702567, -1214068102,
  -541120421, 1898656429, -236825530, 1505866267, 1252462132, -981007520,
  1502096471, -2134644056, 483221797, 1276403836, 541133290, -1234093967,
  350748780, 257941070, 1030457090, 434988890, -1098135432, -1000556640,
  -577128022, 644806294, -787536281, -1288346343, 998079404, 1259353935,
  955771631, -958377466, 1746756252, 451579658, 1913409243, -952026299,
  -1556035958, -830279881, 834744289, -1878491428, 700000962, -1027245802,
  1393574384, -1260409147, -841420884, 892132797, 1494730226, -1649181766,
  1651097838, -1041807403, -1916675721, -1324525963, 157405899, -655788033,
  -1943555237, -79747022, 339721623, -138341083, 1111902411, -435322914,
  -533294200, -190220608, -1718346014, -1631301894, 1706265243, 745533899,
  1351941230, 1803009594, -1218191958, 1467751062, 84368433, -711251880,
  1699423788, -768792716, 846639904, 2103267723, -2095288070, -440571408,
  -362144485, 2020468971, 352105963, -849211036, -1272592429, 1743440467,
  2020667861, -1649992312, 172682343, 816705364, -1990206923, 902689869,
  -298510060, 164207498, 190378213, 242531543, 113383268, 304810777,
  -1081099373, 819221134, -1100982926, -855941239, 1091308887, -934548124,
  520508733, -1381763773, -491593287, -2143492665, 700894653, -2049034808,
  -160942046, -2009323577, 1464245054, 1584746011, -768646852, -993282698,
  1265838699, -1873820824, 575704373, -986682955, 1270688416, 88587481,
  -1723991633, -409928242, 866669946, -483811323, -181759253, -963525431,
  -1686612238, -1663460076, -1128449775, -1368922329, 122318131, 795862385,
  528576131, -19927090, 1369299478, 1285665642, -738964611, 1328292127,
  552041252, -1431494354, -1205275362, 42768297, -1329537238, -449177266,
  943925221, 987016465, -945138414, -270064876, 1650366626, -369252552,
  582030210, -1229235374, 147901387, -517510506, -1609742888, -1086838308,
  1391998445, -313975512, -613392078, 855706229, 1475706341, -1112105406,
  2032001400, 1565777625, 2030937777, 435522421, 1823527907, -691390605,
  -827253664, 1057171580, -314146639, -630099999, -1347514552, 478716232,
  -1533658804, -1425371979, 761987780, 1560243817, -1945893959, 1205759225,
  -959343783, -576742354, -154125407, -1158108776, 1183788580, 1354198127,
  -1534207721, -823991517, -170534462, -912524170, 1858513573, 467072185,
  2091040157, -1765027018, -1659401643, -1173890143, -1912754057, -84568053,
  2010781784, -921970156, 944508352, -922040609, 1055102010, 1018688871,
  -1186761311, -2012263648, 1311654161, 277659086, 2029602288, 1127061510,
  1029452642, 285677123, -188521091, -641039012, 653836416, -805916340,
  -1644860596, 1352872213, 691634876, -1477113308, -748430369, 1030697363,
  -2007864449, -1196662616, 1313997192, 177342476, -566676450, -1118618118,
  1697953104, 344671484, -1489783116, -889507873, 1259591310, -716567168,
  2116447062, 324368527, 1789366816, 1558930442, 1950250221, -785460151,
  1174714258, -430047304, -859487565, -580633932, 607732845, -1128150220,
  1544355315, 1460298016, -1771194297, 1215703690, 277231808, -416020628,
  -418936577, -1724839216, 404731389, 1058730508, -1508366681, 229883053,
  -572310243, 1883189553, 931286849, 1659300867, -94236383, -241524462,
  548020458, -302406981, 579986475, 73468197, -984957614, 1554382245,
  2084807492, -1456802798, -1105192593, 629440327, -16313961, -2102585261,
  1873675206, 161035128, 1497033351, 1990150811, -499405222, 304019482,
  41935663, -805987182, -571699268, 1748462913, 2096239823, -116359807,
  -1871127553, -1074832534, -1558866192, 231353861, 2122854560, -2102323721,
  -281462361, -343403210, -673268171, 1776058383, 1581561150, 2059580579,
  768848632, 1347190372, -1701705879, 245282007, -563267886, -592558289,
  1662399958, 1390406821, -1522485580, -706446863, 2069516289, -301855859,
  -778346387, -1454093198, 1249083752, -1760506745, 262193320, 630751125,
  -1495939124, -29980580, -1989626563, 659039376, -329477132, -1003507166,
  -1322549020, 358606508, -2052572059, 1848014133, 1826958586, -1004948862,
  -1775370541, 2134177912, -1739214473, 1892700918, 926629675, -1042761322,
  2020075900, 606370962, -1256609305, 117577265, -586848924, 191368285,
  1653535275, -1329269701, -375879127, -1089901406, 1206489978, 534223924,
  -1042752982, -1178316881, -445594741, -1501682065, -1598136839,
  -467688289, 750784023, 1781080461, 1729380226, 16906088, 862168532,
  -2037752683, 1455274138, -1491220107, 1058323960, 1711530558, 1355062750,
  227640096, 396568027, -173579098, -408975801, -993618329, -1470751562,
  371076647, 209563718, 2015405719, -723460281, -1423934420, -2089643958,
  353260489, 2084264341, -792676687, 701391030, -1440658244, 1479321011,
  1907822880, 1232524257, -256712289, 401077577, 621808069, 868263613,
  1244930119, 2020996902, 117483907, 1341376744, -1936988014, -445200547,
  -843751811, -435291191, 1041695743, 476132726, -1226874735, -1436046747,
  -297047422, 1739645396, 1948680937, -718144374, 1141983978, 1673650568,
  -197244350, 1604464002, 1424069853, -485626505, 1708710014, -849136541,
  1573778103, 530360999, 1777767203, 1376958336, -1088364352, 1826167753,
  742735448, -1386211659, -1991323164, -444115655, -443055378, -1586901006,
  -1741686587, 1925818034, -2118916824, 803890920, -1481793154, 992278937,
  1302616410, 444517030, 1393144770, -2025632978, 1902300505, -1683582981,
  800654133, 873850324, -619580878, -2002070410, -2024936385, 1978986634,
  2012024264, 675768872, 389435615, -867217540, 231209167, -303917385,
  1445676969, -1385982721, 1310476490, 580273453, -160600202, -1330895874,
  487110497, 1124384798, 227637416, -1829783306, 1014818058, -1336870683,
  -1042199518, -468525587, -1186267363, -472843891, 1215617600, -2056648329,
  -873216891, 156780951, -1883246047, -842549253, -717684332, 760531638,
  1074787431, 786267513, 814031289, -561255343, -110302255, -1837376592,
  989669060, -81350614, 546038730, 222899882, 1298746805, 1791615733,
  1565630269, 1516024174, 421691479, 1860326051, -1973359550, 1854393443,
  -1401468528, -158562295, 1509929255, -124024738, -462937489, 259890715,
  -1515121317, -289511197, -913738664, 698079062, -1631229382, -507275144,
  1897739663, -1118192766, -1687033399, 61405556, -1913606579, -473308896,
  -259107170, -576944609, -1689355510, 322156799, 545090192, 127425176,
  -1815211748, -2070235628, -1172529316, 599259550, -910906653, 1797380363,
  -938649427, 142991392, 504559631, 1208867355, -807699247, -616021271,
  -254935281, -57151221, -1095534993, 1998380318, 1772459584, 713271407,
  -1197898266, 808881935, -308133481, -1314455137, 284321772, -743117625,
  -1622364240, -1667535152, 118713606, 1053615347, -2072876023, -178189072,
  -828319551, 2047304928, -1311435786, -1970672907, -747972100, 86806159,
  -436088421, 1464645587, 735840899, 32600466, -190473426, -735703440,
  482872155, 475662392, -713681085, 1424078728, -150668609, -1137197868,
  -1682762563, -48035649, 1143959866, -1542015129, 284920371, -1587695586,
  -625236551, -753893357, -433976266, -1329796037, -1636712478, 1686783454,
  27839146, 1748631474, -879528256, 2057796026, 773734654, 112269667,
  -2011541314, 1517797297, -1943171794, 268166111, -1037010413, -1945824504,
  -1672323792, 306260758, -692968628, -701704965, -462980996, 939188824,
  553289792, 1790245000, 2093793129, -658085781, -186055037, -2130433650,
  -1013235433, 1190870089, -2126586963, -1509655742, -1291895256,
  -1427857845, 309538950, 388316741, 259659733, -1895092434, 110126220,
  -170175575, -419430224, -696234084, -832170948, -353431720, -797675726,
  -1644136054, 715163272, -1305904349, -145786463, -99586244, -695450446,
  -871327102, -725496060, 952863853, -688441983, -1729929460, -103732092,
  1059054528, 568873585, -982665223, -128672783, 2099418320, 1508239336,
  -2089480835, -390935727, 664306522, -1607364342, -163246802, -1121295140,
  -128375779, -615694409, -2079391797, 760542037, 677761593, -750117849,
  -1060525080, 2128437080, 525250908, 1987657172, 2032530557, -2011247936,
  1942775263, 1681562788, 688229491, -803856505, 684707948, 1308988965,
  1455480037, 790659611, 1557968784, -383203149, -361510986, -742575828,
  558837193, -1214977424, 1253274105, -119513513, -993964385, -33438767,
  -177452803, 1186928041, -2073533871, 1188528559, 1896514695, 1200128512,
  1930588755, -1914141443, 1534656032, -1192989829, -1848274656, -220848455,
  1001806509, 1298797392, 1533031884, -1912322446, 1705583815, 1568094347,
  -1397640627, 807828512, -1852996497, -1529733505, -1575634185,
  -1280270160, -1567624159, -1861904922, 1276738579, 1163432999, 626879833,
  316942006, -1871138342, 1341039701, 1595907877, 1950911580, 1634717748,
  1071476055, -809354290, -1161553341, -2081621710, -2085557943, 19360224,
  322135580, -698485151, 1267663094, -233890834, -126361189, -1426257522,
  1094007921, 500179855, -283548002, -1678987343, 1946999943, 1489410849,
  2089571262, 1430799093, 1961848046, -99462663, -552833264, 1168700661,
  -1783882181, 2089196401, 1092839657, 914488673, 80263859, -2140947098,
  -726384741, -1022448237, 2113887675, 1485770846, -112922517, 1995461466,
  774613726, 944068011, 1521975359, 289086919, -386920759, -1960513175,
  358460021, -238698524, -1913640563, -1000324864, 1731755224, -1271586254,
  -1917469655, 2134162829, -828097534, -1089292503, -1514835999, 1682931514,
  -482307169, 2110243841, 115744834, -2038340170, 65889188, -539445712,
  -1713206408, -1842396726, -1659545588, -909558923, 860164922, 1328713040,
  1044007120, -2103807103, -1073990344, -1312783785, -884980824, -705318011,
  -1263408788, -2032228692, -1732844111, -1813827156, 1462566279,
  1179250845, 1732421772, 604429013, -92284336, -1192166516, 304654351,
  1998552034, -1802461575, -1802704071, -1704833934, -976264396, 1005840702,
  2108843914, 1363909309, 843040834, -1039625241, 1285007226, 91610001,
  418426329, 678422358, -945360697, -440008081, -1053091357, 425719777,
  -1372778676, 591912153, 1229089037, -56663158, 2140251400, 830257037,
  763914157, 175610373, -2105655963, -1040826150, 1174443038, 339290593,
  346618443, -180504100, -1363190515, 210620018, 1028894425, 573529714,
  698460117, 136999397, 1015621712, -1401813739, -297990684, -1820934845,
  -1299093313, 1299361369, -366522415, 91527707, 1113466178, -956229484,
  22204763, -1394374195, -1912666711, -1453789804, 1613408399, -169509567,
  1350520309, 540761213, -2086682848, 1095131491, -812787911, 1860108594,
  -1121378737, -1667252487, -486084366, 166519760, 1609891237, 728218405,
  291075010, 646168382, 108462277, -1616661910, 1016600360, 2099958568,
  27934736, 183821196, 13660496, -805589719, 936068730, -439037934,
  1414622584, 215845485, -1352304469, -1817427526, -1318710977, -110207199,
  228524335, 1704746590, 998293651, -1521016702, -641956531, -2089808167,
  2094404052, -1446381065, -662186492, 1670154584, 9637833, 493925511,
  660047318, 1197537103, 1696017374, -204994399, -1104145601, -852330465,
  -1936369658, -829716674, -1255255217, 1264013799, 1642611772, -652520861,
  777247164, 2028895987, -1424241853, -54367829, -1940161761, -1802831079,
  -449405299, 838242661, -323055438, 794295411, -136989378, -446686673,
  -421252799, -16777216,
};

int
main (int argc, char *argv[])
{
  char buf[1024];
  int i;

  GNUNET_log_setup ("test-crypto-crc", "WARNING", NULL);
  for (i = 0; i < 1024; i++)
    buf[i] = (char) i;
  for (i = 0; i < 1024; i++)
    if (expected[i] != GNUNET_CRYPTO_crc32_n (&buf[i], 1024 - i))
      return 1;
  return 0;
}
