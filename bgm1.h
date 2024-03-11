/*
# ['opll_mode 0', 'tempo 104', 'title {"PAUSE"}']
g opll_mode 0
g tempo 104
g title {"PAUSE"}
k 9:vs []
k A:vs ['l8v11@2o6q6a32f+32g32a32b32']
k B:vs ['l8v14@11o6b16r16g32']
k C:vs ['l8v12@11r64o6b16r16g64']
k D:vs ['l8@2v11o7f+32d32e32f+32g32']
k E:vs ['l8@2v9r64o7f+32d32e32f+32g64']
k F:vs ['l8@2v10r64o6a32f+32g32a32b64c16&<<c1']
k G:vs []
k H:vs []
{'@': [], '#': ['opll_mode 0', 'tempo 104', 'title {"PAUSE"}'], '9': '', 'A': 'l8v11@2o6q6a32f+32g32a32b32', 'B': 'l8v14@11o6b16r16g32', 'C': 'l8v12@11r64o6b16r16g64', 'D': 'l8@2v11o7f+32d32e32f+32g32', 'E': 'l8@2v9r64o7f+32d32e32f+32g64', 'F': 'l8@2v10r64o6a32f+32g32a32b64c16&<<c1', 'G': '', 'H': ''}
{'@': {}, '#': {'opll_mode': 0, 'tempo': 104, 'title': 'PAUSE'}, '9': [], 'A': [['l', 24.0], ['v', 11], ['@', 2], ['o', 6], ['q', 6], ['tone', 'a', 6.0], ['tone', 'f+', 6.0], ['tone', 'g', 6.0], ['tone', 'a', 6.0], ['tone', 'b', 6.0]], 'B': [['l', 24.0], ['v', 14], ['@', 11], ['o', 6], ['tone', 'b', 12.0], ['tone', 'r', 12.0], ['tone', 'g', 6.0]], 'C': [['l', 24.0], ['v', 12], ['@', 11], ['tone', 'r', 3.0], ['o', 6], ['tone', 'b', 12.0], ['tone', 'r', 12.0], ['tone', 'g', 3.0]], 'D': [['l', 24.0], ['@', 2], ['v', 11], ['o', 7], ['tone', 'f+', 6.0], ['tone', 'd', 6.0], ['tone', 'e', 6.0], ['tone', 'f+', 6.0], ['tone', 'g', 6.0]], 'E': [['l', 24.0], ['@', 2], ['v', 9], ['tone', 'r', 3.0], ['o', 7], ['tone', 'f+', 6.0], ['tone', 'd', 6.0], ['tone', 'e', 6.0], ['tone', 'f+', 6.0], ['tone', 'g', 3.0]], 'F': [['l', 24.0], ['@', 2], ['v', 10], ['tone', 'r', 3.0], ['o', 6], ['tone', 'a', 6.0], ['tone', 'f+', 6.0], ['tone', 'g', 6.0], ['tone', 'a', 6.0], ['tone', 'b', 3.0], ['tone', 'c', 12.0], ['&'], ['<'], ['<'], ['tone', 'c', 192.0]], 'G': [], 'H': []}
*/
u8 const bgm1_0[24]={
  0,PVOLUME,52,/*PTONE,*/69,3,PKEYOFF,1,/*PTONE,*/66,4,PKEYOFF,1,/*PTONE,*/67,3,PKEYOFF,1,/*PTONE,*/69,3,PKEYOFF,1,/*PTONE,*/71,4,PKEYOFF,1,PEND};
u8 const bgm1_1[10]={
  0,PVOLUME,193,/*PTONE,*/71,9,PKEYOFF,8,/*PTONE,*/67,5,PEND};
u8 const bgm1_2[12]={
  0,PVOLUME,195,PKEYOFF,2,/*PTONE,*/71,9,PKEYOFF,8,/*PTONE,*/67,3,PEND};
u8 const bgm1_3[14]={
  0,PVOLUME,52,/*PTONE,*/78,4,/*PTONE,*/74,5,/*PTONE,*/76,4,/*PTONE,*/78,4,/*PTONE,*/79,5,PEND};
u8 const bgm1_4[16]={
  0,PVOLUME,54,PKEYOFF,2,/*PTONE,*/78,4,/*PTONE,*/74,5,/*PTONE,*/76,4,/*PTONE,*/78,4,/*PTONE,*/79,3,PEND};
u8 const bgm1_5[20]={
  0,PVOLUME,53,PKEYOFF,2,/*PTONE,*/69,4,/*PTONE,*/66,5,/*PTONE,*/67,4,/*PTONE,*/69,4,/*PTONE,*/71,3,/*PTONE,*/60,8,/*PTONE,*/36,139,PEND};
u8* const bgm1[]={bgm1_0,bgm1_1,bgm1_2,bgm1_3,bgm1_4,bgm1_5,};
