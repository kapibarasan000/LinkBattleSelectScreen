#include "defines.h"
#include "../include/battle.h"
#include "../include/battle_2.h"
#include "../include/bg.h"
#include "../include/field_weather.h"
#include "../include/gpu_regs.h"
#include "../include/link.h"
#include "../include/main.h"
#include "../include/malloc.h"
#include "../include/menu.h"
#include "../include/party_menu.h"
#include "../include/palette.h"
#include "../include/pokemon.h"
#include "../include/pokemon_icon.h"
#include "../include/pokemon_summary_screen.h"
#include "../include/script.h"
#include "../include/sound.h"
#include "../include/sprite.h"
#include "../include/string_util.h"
#include "../include/text.h"
#include "../include/text_window.h"
#include "../include/window.h"

#include "../include/constants/songs.h"
#include "../include/constants/species.h"
#include "../include/constants/trainers.h"
#include "../include/gba/io_reg.h"

#include "../include/new/ram_locs_battle.h"
#include "../include/new/util.h"


enum {
    CB_MAIN_MENU,
    CB_SELECTED_MON,
    CB_SHOW_MON_SUMMARY,
    CB_READY_WAIT,
    CB_CLEAR_MSG,
    CB_FADE_TO_START_BATTLE,
    CB_WAIT_TO_START_BATTLE,
    CB_START_LINK_BATTLE,
    CB_WAIT_TO_START_RFU_BATTLE,
    CB_IDLE = 100,
};

enum {
    MSG_STANDBY,
    MSG_MON_CANT_BE_SELECT,
};

enum {
    QUEUE_SEND_DATA,
    QUEUE_STANDBY,
    QUEUE_MON_CANT_BE_SELECT,
};

#define QUEUE_DELAY_MSG   3
#define QUEUE_DELAY_DATA  5

#define GFXTAG_CURSOR          300
#define PALTAG_CURSOR    2345

#define GFXTAG_ORDER_NUMBER          301
#define PALTAG_ORDER_NUMBER    2346

#define gText_MaleSymbol ((void*) 0x083DD84F)
#define gText_FemaleSymbol ((void*) 0x083DD851)
#define gText_Level ((void*) 0x083DD853)
#define gText_Select ((void*) 0x083DDDD0)
#define gText_Deselect ((void*) 0x083DDDD6)
#define gText_Summary ((void*) 0x083DDDC2)
#define gText_Trade_CommunicationStandby ((void*) 0x083E1FC9)
#define gTradeText_ChooseAPokemon ((void*) 0x083DE388)
#define gText_confirm ((void*) 0x083E037F)

extern const u8 gText_PkmnCantBeSelect[];
extern const u8 OrderNumberTiles[];
extern const u16 OrderNumberPal[];

#define sLinkBattleSelectScreen (*((struct LinkBattleSelectScreen**) 0x2031CDC))

enum {
    CURSOR_ANIM_NORMAL,
    CURSOR_ANIM_ON_CANCEL,
};

// Values for signaling to/from the link partner
enum {
    LINK_STATUS_NONE,
    LINK_STATUS_READY,
    LINK_STATUS_CANCEL,
};

enum {
    LS_WIN_MSG,
    LS_WIN_OPTIONS,
    LS_WIN_PLAYER_MON_1,
    LS_WIN_PLAYER_MON_2,
    LS_WIN_PLAYER_MON_3,
    LS_WIN_PLAYER_MON_4,
    LS_WIN_PLAYER_MON_5,
    LS_WIN_PLAYER_MON_6,
    LS_WIN_OPPONENT_MON_1,
    LS_WIN_OPPONENT_MON_2,
    LS_WIN_OPPONENT_MON_3,
    LS_WIN_OPPONENT_MON_4,
    LS_WIN_OPPONENT_MON_5,
    LS_WIN_OPPONENT_MON_6,
    LS_WIN_PLAYER_NAME,
    LS_WIN_OPPONENT_NAME,
    LS_WIN_BOTTOM_TEXT,
    LS_WIN_CONFIRM,
};

struct LinkBattleSelectScreen
{
    u8 partySpriteIds[2][PARTY_SIZE];
    u8 cursorSpriteId;
    u8 cursorPosition;
    u8 orderNumberSpriteIds[PARTY_SIZE];
    u8 partyCounts[2];
    u8 maxMon;
    bool8 optionsActive[PARTY_SIZE + 1];
    u8 selectedId[PARTY_SIZE];
    u8 bufferPartyState;
    u8 callbackId;
    u8 playerSelectStatus;
    u8 playerConfirmStatus;
    u8 partnerConfirmStatus;
    u8 ReducePartyState;
    struct Pokemon selectedMon[PARTY_SIZE];
    u16 linkData[20];
    u8 timer;
    struct {
        bool8 active;
        u16 delay;
        u8 actionId;
    } queuedActions[4];
    u16 tilemapBuffer[BG_SCREEN_SIZE / 2];
};

static void VBlankCB_LinkBattleSelectScreen(void);
static void CB2_LinkBattleSelectScreen(void);
static void LoadLinkBattleSelectScreenBgGfx(u8 state);
static void SetActiveMenuOptions(void);
static u8 BufferLinkBattleParties(void);
static void CB1_UpdateLink(void);
static void RunLinkBattleSelectScreenCallback(void);
static void PrintPlayerName(u8 whichParty);
static void PrintLevelAndGender(u8 whichParty, u8 windowId, u8 level, u8 gender);
static void PrintPartyLevelsAndGenders(u8 side);
static void PrintConfirm(void);
static void QueueAction(u16 delay, u8 actionId);
static void DoQueuedActions(void);
static void PrintMessageAndWindow(u8 strIdx);
static bool8 LoadUISpriteGfx(void);
static void DrawBottomRowText(const u8 *text);
static void ClearMsgWindow(u8 windowId);
static void PrintMonBoxes(u8 whichParty);

static const struct OamData sOamData_Cursor = {
    .shape = SPRITE_SHAPE(64x32),
    .size = SPRITE_SIZE(64x32),
    .priority = 2
};

static const union AnimCmd sAnim_Cursor_Normal[] = {
    ANIMCMD_FRAME(0, 5),
    ANIMCMD_END
};

static const union AnimCmd sAnim_Cursor_OnCancel[] = {
    ANIMCMD_FRAME(32, 5),
    ANIMCMD_END
};

static const union AnimCmd *const sAnims_Cursor[] = {
    [CURSOR_ANIM_NORMAL]    = sAnim_Cursor_Normal,
    [CURSOR_ANIM_ON_CANCEL] = sAnim_Cursor_OnCancel
};

static const struct SpriteSheet sCursor_SpriteSheet = {
    .data = (void*) 0x82201D0,
    .size = 0x800,
    .tag = GFXTAG_CURSOR
};

static const struct SpritePalette sCursor_SpritePalette = {
    .data = (void*) 0x821D310,
    .tag = PALTAG_CURSOR
};

static const struct SpriteTemplate sSpriteTemplate_Cursor = {
    .tileTag = GFXTAG_CURSOR,
    .paletteTag = PALTAG_CURSOR,
    .oam = &sOamData_Cursor,
    .anims = sAnims_Cursor,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCallbackDummy
};

static const struct OamData sOamData_OrderNumber = {
    .shape = SPRITE_SHAPE(16x16),
    .size = SPRITE_SIZE(16x16),
    .priority = 1
};

static const union AnimCmd sAnim_OrderNumber_1[] = {
    ANIMCMD_FRAME(0, 0),
    ANIMCMD_END
};

static const union AnimCmd sAnim_OrderNumber_2[] = {
    ANIMCMD_FRAME(4, 0),
    ANIMCMD_END
};

static const union AnimCmd sAnim_OrderNumber_3[] = {
    ANIMCMD_FRAME(8, 0),
    ANIMCMD_END
};

static const union AnimCmd sAnim_OrderNumber_4[] = {
    ANIMCMD_FRAME(12, 0),
    ANIMCMD_END
};

static const union AnimCmd sAnim_OrderNumber_5[] = {
    ANIMCMD_FRAME(16, 0),
    ANIMCMD_END
};

static const union AnimCmd sAnim_OrderNumber_6[] = {
    ANIMCMD_FRAME(20, 0),
    ANIMCMD_END
};

static const union AnimCmd *const sAnims_OrderNumber[] = {
    sAnim_OrderNumber_1,
    sAnim_OrderNumber_2,
    sAnim_OrderNumber_3,
    sAnim_OrderNumber_4,
    sAnim_OrderNumber_5,
    sAnim_OrderNumber_6
};

static const struct SpriteSheet sOrderNumber_SpriteSheet = {
    .data = OrderNumberTiles,
    .size = (16 * 16 * 6) / 2,
    .tag = GFXTAG_ORDER_NUMBER
};

static const struct SpritePalette sOrderNumber_SpritePalette = {
    .data = OrderNumberPal,
    .tag = PALTAG_ORDER_NUMBER
};

static const struct SpriteTemplate sSpriteTemplate_OrderNumber = {
    .tileTag = GFXTAG_ORDER_NUMBER,
    .paletteTag = PALTAG_ORDER_NUMBER,
    .oam = &sOamData_OrderNumber,
    .anims = sAnims_OrderNumber,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCallbackDummy
};

#define DIR_UP    0
#define DIR_DOWN  1
#define DIR_LEFT  2
#define DIR_RIGHT 3

#define COL0_X 1
#define COL1_X 8
#define COL2_X 16
#define COL3_X 23
#define ROW0_Y 5
#define ROW1_Y 10
#define ROW2_Y 15
#define ROW3_Y 18

static const u8 sMonSpriteCoords[(PARTY_SIZE * 2) + 1][2] = {
    [LINK_PLAYER] =
        {COL0_X, ROW0_Y},
        {COL1_X, ROW0_Y},
        {COL0_X, ROW1_Y},
        {COL1_X, ROW1_Y},
        {COL0_X, ROW2_Y},
        {COL1_X, ROW2_Y},
    [LINK_PARTNER * PARTY_SIZE] =
        {COL2_X, ROW0_Y},
        {COL3_X, ROW0_Y},
        {COL2_X, ROW1_Y},
        {COL3_X, ROW1_Y},
        {COL2_X, ROW2_Y},
        {COL3_X, ROW2_Y},
    // Cancel
    {COL3_X, ROW3_Y},
};

static const u8 sMonLevelCoords[PARTY_SIZE * 2][2] = {
    [LINK_PLAYER] =
        { 5,  4},
        {12,  4},
        { 5,  9},
        {12,  9},
        { 5, 14},
        {12, 14},
    [LINK_PARTNER * PARTY_SIZE] =
        {20,  4},
        {27,  4},
        {20,  9},
        {27,  9},
        {20, 14},
        {27, 14},
};

static const u8 sMonBoxCoords[PARTY_SIZE * 2][2] = {
    [LINK_PLAYER] =
        { 1,  3},
        { 8,  3},
        { 1,  8},
        { 8,  8},
        { 1, 13},
        { 8, 13},
    [LINK_PARTNER * PARTY_SIZE] =
        {16,  3},
        {23,  3},
        {16,  8},
        {23,  8},
        {16, 13},
        {23, 13},
};

static const u8 *const sMessages[] = {
    [MSG_STANDBY]                    = gText_Trade_CommunicationStandby,
    [MSG_MON_CANT_BE_SELECT]         = gText_PkmnCantBeSelect, 
};

static const struct TextColor sTextColor_PartyMonNickname = { TEXT_COLOR_TRANSPARENT, TEXT_COLOR_WHITE, TEXT_COLOR_DARK_GREY };
static const struct TextColor sMaleColors = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_LIGHT_BLUE, TEXT_COLOR_BLUE};
static const struct TextColor sFemaleColors = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_LIGHT_RED, TEXT_COLOR_RED};

static const struct BgTemplate sBgTemplates[] = {
    {
        .bg = 0,
        .charBaseIndex = 2,
        .mapBaseIndex = 31,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 0,
        .baseTile = 0x000
    }, {
        .bg = 1,
        .charBaseIndex = 1,
        .mapBaseIndex = 7,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 1,
        .baseTile = 0x000
    }, {
        .bg = 2,
        .charBaseIndex = 0,
        .mapBaseIndex = 6,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 2,
        .baseTile = 0x000
    }, {
        .bg = 3,
        .charBaseIndex = 0,
        .mapBaseIndex = 5,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 3,
        .baseTile = 0x000
    }
};

static const struct WindowTemplate sWindowTemplates[] = {
    {
        .bg = 0,
        .tilemapLeft = 4,
        .tilemapTop = 7,
        .width = 22,
        .height = 4,
        .paletteNum = 15,
        .baseBlock = 0x01e
    }, {
        .bg = 0,
        .tilemapLeft = 19,
        .tilemapTop = 15,
        .width = 10,
        .height = 4,
        .paletteNum = 15,
        .baseBlock = 0x076
    }, {
        .bg = 1,
        .tilemapLeft = 1,
        .tilemapTop = 5,
        .width = 8,
        .height = 2,
        .paletteNum = 13,
        .baseBlock = 0x0a6
    }, {
        .bg = 1,
        .tilemapLeft = 8,
        .tilemapTop = 5,
        .width = 8,
        .height = 2,
        .paletteNum = 13,
        .baseBlock = 0x0b6
    }, {
        .bg = 1,
        .tilemapLeft = 1,
        .tilemapTop = 10,
        .width = 8,
        .height = 2,
        .paletteNum = 13,
        .baseBlock = 0x0c6
    }, {
        .bg = 1,
        .tilemapLeft = 8,
        .tilemapTop = 10,
        .width = 8,
        .height = 2,
        .paletteNum = 13,
        .baseBlock = 0x0d6
    }, {
        .bg = 1,
        .tilemapLeft = 1,
        .tilemapTop = 15,
        .width = 8,
        .height = 2,
        .paletteNum = 13,
        .baseBlock = 0x0e6
    }, {
        .bg = 1,
        .tilemapLeft = 8,
        .tilemapTop = 15,
        .width = 8,
        .height = 2,
        .paletteNum = 13,
        .baseBlock = 0x0f6
    }, {
        .bg = 1,
        .tilemapLeft = 16,
        .tilemapTop = 5,
        .width = 8,
        .height = 2,
        .paletteNum = 13,
        .baseBlock = 0x106
    }, {
        .bg = 1,
        .tilemapLeft = 23,
        .tilemapTop = 5,
        .width = 8,
        .height = 2,
        .paletteNum = 13,
        .baseBlock = 0x116
    }, {
        .bg = 1,
        .tilemapLeft = 16,
        .tilemapTop = 10,
        .width = 8,
        .height = 2,
        .paletteNum = 13,
        .baseBlock = 0x126
    }, {
        .bg = 1,
        .tilemapLeft = 23,
        .tilemapTop = 10,
        .width = 8,
        .height = 2,
        .paletteNum = 13,
        .baseBlock = 0x136
    }, {
        .bg = 1,
        .tilemapLeft = 16,
        .tilemapTop = 15,
        .width = 8,
        .height = 2,
        .paletteNum = 13,
        .baseBlock = 0x146
    }, {
        .bg = 1,
        .tilemapLeft = 23,
        .tilemapTop = 15,
        .width = 8,
        .height = 2,
        .paletteNum = 13,
        .baseBlock = 0x156
    }, {
        .bg = 1,
        .tilemapLeft = 4,
        .tilemapTop = 0,
        .width = 7,
        .height = 2,
        .paletteNum = 13,
        .baseBlock = 0x166
    }, {
        .bg = 1,
        .tilemapLeft = 19,
        .tilemapTop = 0,
        .width = 7,
        .height = 2,
        .paletteNum = 15,
        .baseBlock = 0x176
    }, {
        .bg = 1,
        .tilemapLeft = 1,
        .tilemapTop = 18,
        .width = 20,
        .height = 2,
        .paletteNum = 15,
        .baseBlock = 0x186
    }, {
        .bg = 1,
        .tilemapLeft = 25,
        .tilemapTop = 18,
        .width = 5,
        .height = 2,
        .paletteNum = 15,
        .baseBlock = 0x1AE
    }, 
    DUMMY_WIN_TEMPLATE,
};

static void VBlankCB_LinkBattleSelectScreen(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

static void InitLinkBattleSelectScreen(void)
{
    ResetSpriteData();
    FreeAllSpritePalettes();
    ResetTempTileDataBuffers();
    ResetTasks();
    ResetPaletteFade();
    DmaFill16(3, 0, VRAM, VRAM_SIZE);
	DmaFill32(3, 0, OAM, OAM_SIZE);
	DmaFill16(3, 0, PLTT, PLTT_SIZE);

    gPaletteFade->bufferTransferDisabled = TRUE;

    SetVBlankCallback(VBlankCB_LinkBattleSelectScreen);
    LoadPalette((void*) 0x83E30AC, 0xF0, 0x14);
    LoadPalette((void*) 0x83E30AC, 0xD0, 0x14);
    ResetBgsAndClearDma3BusyFlags(FALSE);
    InitBgsFromTemplates(0, sBgTemplates, ARRAY_COUNT(sBgTemplates));
    SetBgTilemapBuffer(3, sLinkBattleSelectScreen->tilemapBuffer);

    if (InitWindows(sWindowTemplates))
    {
        int i;
        DeactivateAllTextPrinters();
        for (i = 0; i < 17; i++)
        {
            ClearWindowTilemap(i);
            FillWindowPixelBuffer(i, PIXEL_FILL(0));
        }
        FillBgTilemapBufferRect(0, 0, 0, 0, 30, 20, 15);
        TextWindow_SetStdFrame0_WithPal(0, 0x014, 0xC0);
        TextWindow_SetUserSelectedFrame(LS_WIN_OPTIONS, 0x001, 0xE0);
        LoadMonIconPalettes();
        sLinkBattleSelectScreen->bufferPartyState = 0;
        sLinkBattleSelectScreen->callbackId = CB_MAIN_MENU;
        sLinkBattleSelectScreen->playerConfirmStatus = LINK_STATUS_NONE;
        sLinkBattleSelectScreen->partnerConfirmStatus = LINK_STATUS_NONE;
        sLinkBattleSelectScreen->timer = 0;
        sLinkBattleSelectScreen->maxMon = Var8004 == USING_DOUBLE_BATTLE ? 4 : 3;
    }
}

void HealMon(struct Pokemon* mon)
{
	u32 none = 0;
	u16 maxHP = GetMonData(mon, MON_DATA_MAX_HP, NULL);

	//Restore HP.
	SetMonData(mon, MON_DATA_HP, &maxHP);

	//Restore PP.
	MonRestorePP(mon);

	//Restore Status.
	SetMonData(mon, MON_DATA_STATUS, &none);
}

void SetTeamToLevel50(void)
{
	for (u32 i = 0; i < PARTY_SIZE; ++i)
	{
		u16 species = GetMonData(&gPlayerParty[i], MON_DATA_SPECIES2, NULL);
		if (species != SPECIES_NONE && species != SPECIES_EGG)
		{
			u32 exp = gExperienceTables[gBaseStats[species].growthRate][50];
			SetMonData(&gPlayerParty[i], MON_DATA_EXP, &exp);
			CalculateMonStats(&gPlayerParty[i]);
			HealMon(&gPlayerParty[i]);
		}
	}
}

void CB2_CreateLinkBattleSelectScreen(void)
{
    int i;

    switch (gMain.state)
    {
    case 0:
        sLinkBattleSelectScreen = AllocZeroed(sizeof(struct LinkBattleSelectScreen));
        InitLinkBattleSelectScreen();
        gMain.state++;
        break;
    case 1:
        gPaletteFade->bufferTransferDisabled = FALSE;

        for (i = 0; i < PARTY_SIZE; i++)
            CreateMon(&gEnemyParty[i], SPECIES_NONE, 0, 32, FALSE, 0, OT_ID_PLAYER_ID, 0);

        PrintMessageAndWindow(MSG_STANDBY);
        ShowBg(0);

        if (!gReceivedRemoteLinkPlayers)
        {
            gLinkType = LINKTYPE_TRADE_CONNECTING;
            sLinkBattleSelectScreen->timer = 0;

            if (gWirelessCommType)
            {
                SetWirelessCommType1();
                OpenLink();
                CreateTask_RfuIdle();
            }
            else
            {
                OpenLink();
                gMain.state++;
            }
            if (gWirelessCommType == 0)
                CreateTask(Task_WaitForLinkPlayerConnection, 1);
        }
        else
        {
            gMain.state = 4;
        }
        break;
    case 2:
        sLinkBattleSelectScreen->timer++;
        if (sLinkBattleSelectScreen->timer > 11)
        {
            sLinkBattleSelectScreen->timer = 0;
            gMain.state++;
        }
        break;
    case 3:
        if (GetLinkPlayerCount_2() >= GetSavedPlayerCount())
        {
            if (IsLinkMaster())
            {
                if (++sLinkBattleSelectScreen->timer > 30)
                {
                    CheckShouldAdvanceLinkState();
                    gMain.state++;
                }
            }
            else
            {
                gMain.state++;
            }
        }
        break;
    case 4:
        if (gReceivedRemoteLinkPlayers == TRUE && IsLinkPlayerDataExchangeComplete() == TRUE)
        {
            DestroyTask_RfuIdle();
            CalculatePlayerPartyCount();
            gMain.state++;
            sLinkBattleSelectScreen->timer = 0;
            if (gWirelessCommType)
            {
                Rfu_SetLinkRecovery(TRUE);
                SetLinkStandbyCallback();
            }
        }
        break;
    case 5:
        if (gWirelessCommType)
        {
            if (IsLinkRfuTaskFinished())
            {
                gMain.state++;
                LoadWirelessStatusIndicatorSpriteGfx();
                CreateWirelessStatusIndicatorSprite(0, 0);
            }
        }
        else
        {
            gMain.state++;
        }
        break;
    case 6:
        SetTeamToLevel50();
        gMain.state++;
        break;
    case 7:
        if (BufferLinkBattleParties())
        {
            gMain.state++;
        }
        break;
    case 8:
        CalculateEnemyPartyCount();
        SetGpuReg(REG_OFFSET_DISPCNT, 0);
        SetGpuReg(REG_OFFSET_BLDCNT, 0);
        sLinkBattleSelectScreen->partyCounts[LINK_PLAYER] = gPlayerPartyCount;
        sLinkBattleSelectScreen->partyCounts[LINK_PARTNER] = gEnemyPartyCount;

        for (i = 0; i < sLinkBattleSelectScreen->partyCounts[LINK_PLAYER]; i++)
        {
            struct Pokemon * mon = &gPlayerParty[i];
            sLinkBattleSelectScreen->partySpriteIds[LINK_PLAYER][i] = CreateMonIcon(GetMonData(mon, MON_DATA_SPECIES2, NULL),
                                                                SpriteCB_PokeIcon,
                                                                (sMonSpriteCoords[i][0] * 8) + 14,
                                                                (sMonSpriteCoords[i][1] * 8) - 12,
                                                                1,
                                                                GetMonData(mon, MON_DATA_PERSONALITY, NULL),
                                                                TRUE);
        }

        for (i = 0; i < sLinkBattleSelectScreen->partyCounts[LINK_PARTNER]; i++)
        {
            struct Pokemon * mon = &gEnemyParty[i];
            sLinkBattleSelectScreen->partySpriteIds[LINK_PARTNER][i] = CreateMonIcon(GetMonData(mon, MON_DATA_SPECIES2, NULL),
                                                                SpriteCB_PokeIcon,
                                                                (sMonSpriteCoords[i + PARTY_SIZE][0] * 8) + 14,
                                                                (sMonSpriteCoords[i + PARTY_SIZE][1] * 8) - 12,
                                                                1,
                                                                GetMonData(mon, MON_DATA_PERSONALITY, NULL),
                                                                FALSE);
        }
        gMain.state++;
        break;
    case 9:
        LoadHeldItemIcons();
        DrawHeldItemIconsForTrade(sLinkBattleSelectScreen->partyCounts, sLinkBattleSelectScreen->partySpriteIds[0], LINK_PLAYER);
        gMain.state++;
        break;
    case 10:
        DrawHeldItemIconsForTrade(sLinkBattleSelectScreen->partyCounts, sLinkBattleSelectScreen->partySpriteIds[0], LINK_PARTNER);
        gMain.state++;
        break;
    case 11:
        PrintPlayerName(LINK_PLAYER);
        PrintPlayerName(LINK_PARTNER);
        PrintConfirm();
        DrawBottomRowText(gTradeText_ChooseAPokemon);
        gMain.state++;
        sLinkBattleSelectScreen->timer = 0;
        break;
    case 12:
        if (LoadUISpriteGfx())
            gMain.state++;
        break;
    case 13:
        sLinkBattleSelectScreen->cursorSpriteId = CreateSprite(&sSpriteTemplate_Cursor, sMonSpriteCoords[0][0] * 8 + 32, sMonSpriteCoords[0][1] * 8, 2);
        sLinkBattleSelectScreen->cursorPosition = 0;
        gMain.state++;
        ClearMsgWindow(LS_WIN_MSG);
        break;
    case 14:
        for (i = 0; i < sLinkBattleSelectScreen->partyCounts[LINK_PLAYER]; i++)
        {
            sLinkBattleSelectScreen->orderNumberSpriteIds[i] = CreateSprite(&sSpriteTemplate_OrderNumber, (sMonSpriteCoords[i][0] * 8) + 38, (sMonSpriteCoords[i][1] * 8) - 5, 1);
            gSprites[sLinkBattleSelectScreen->orderNumberSpriteIds[i]].invisible = TRUE;
        }

        gMain.state++;
        break;
    case 15:
        PrintPartyLevelsAndGenders(LINK_PLAYER);
        SetActiveMenuOptions();
        gMain.state++;
        break;
    case 16:
        PrintPartyLevelsAndGenders(LINK_PARTNER);
        gMain.state++;
        // fallthrough
    case 17:
        LoadLinkBattleSelectScreenBgGfx(0);
        gMain.state++;
        break;
    case 18:
        LoadLinkBattleSelectScreenBgGfx(1);
        gMain.state++;
        break;
    case 19:
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        gMain.state++;
        break;
    case 20:
        SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_OBJ_1D_MAP | DISPCNT_OBJ_ON);
        LoadLinkBattleSelectScreenBgGfx(2);
        gMain.state++;
        break;
    case 21:
        if (!gPaletteFade->active)
        {
            gMain.callback1 = CB1_UpdateLink;
            SetMainCallback2(CB2_LinkBattleSelectScreen);
        }
        break;
    }

    RunTextPrinters();
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    UpdatePaletteFade();
}

void CB2_ReturnToLinkBattleSelectScreenFromSummary(void)
{
    int i;

    switch (gMain.state)
    {
    case 0:
        InitLinkBattleSelectScreen();
        gMain.state++;
        break;
    case 1:
        gMain.state++;
        sLinkBattleSelectScreen->timer = 0;
        break;
    case 2:
        gMain.state++;
        break;
    case 3:
        gMain.state++;
        break;
    case 4:
        CalculatePlayerPartyCount();
        gMain.state++;
        break;
    case 5:
        if (gWirelessCommType != 0)
        {
            LoadWirelessStatusIndicatorSpriteGfx();
            CreateWirelessStatusIndicatorSprite(0, 0);
        }
        gMain.state++;
        break;
    case 6:
        gMain.state++;
        break;
    case 7:
        CalculateEnemyPartyCount();
        sLinkBattleSelectScreen->partyCounts[LINK_PLAYER] = gPlayerPartyCount;
        sLinkBattleSelectScreen->partyCounts[LINK_PARTNER] = gEnemyPartyCount;
        ClearWindowTilemap(0);
        PrintPartyLevelsAndGenders(LINK_PLAYER);
        PrintPartyLevelsAndGenders(LINK_PARTNER);
        for (i = 0; i < sLinkBattleSelectScreen->partyCounts[LINK_PLAYER]; i++)
        {
            sLinkBattleSelectScreen->partySpriteIds[LINK_PLAYER][i] = CreateMonIcon(
                GetMonData(&gPlayerParty[i], MON_DATA_SPECIES2, NULL),
                SpriteCB_PokeIcon,
                sMonSpriteCoords[i][0] * 8 + 14,
                sMonSpriteCoords[i][1] * 8 - 12,
                1,
                GetMonData(&gPlayerParty[i], MON_DATA_PERSONALITY, NULL),
                TRUE
            );
        }
        for (i = 0; i < sLinkBattleSelectScreen->partyCounts[LINK_PARTNER]; i++)
        {
            sLinkBattleSelectScreen->partySpriteIds[LINK_PARTNER][i] = CreateMonIcon(
                GetMonData(&gEnemyParty[i], MON_DATA_SPECIES2, NULL),
                SpriteCB_PokeIcon,
                sMonSpriteCoords[i + PARTY_SIZE][0] * 8 + 14,
                sMonSpriteCoords[i + PARTY_SIZE][1] * 8 - 12,
                1,
                GetMonData(&gEnemyParty[i], MON_DATA_PERSONALITY, NULL),
                FALSE
            );
        }
        gMain.state++;
        break;
    case 8:
        LoadHeldItemIcons();
        DrawHeldItemIconsForTrade(sLinkBattleSelectScreen->partyCounts, sLinkBattleSelectScreen->partySpriteIds[0], LINK_PLAYER);
        gMain.state++;
        break;
    case 9:
        DrawHeldItemIconsForTrade(sLinkBattleSelectScreen->partyCounts, sLinkBattleSelectScreen->partySpriteIds[0], LINK_PARTNER);
        gMain.state++;
        break;
    case 10:
        PrintPlayerName(LINK_PLAYER);
        PrintPlayerName(LINK_PARTNER);
        PrintConfirm();
        DrawBottomRowText(gTradeText_ChooseAPokemon);
        gMain.state++;
        sLinkBattleSelectScreen->timer = 0;
        break;
    case 11:
        if (LoadUISpriteGfx())
            gMain.state++;
        break;
    case 12:
        sLinkBattleSelectScreen->cursorPosition = sLastViewedMonIndex;
        sLinkBattleSelectScreen->cursorSpriteId = CreateSprite(&sSpriteTemplate_Cursor,
                                                  sMonSpriteCoords[sLinkBattleSelectScreen->cursorPosition][0] * 8 + 32,
                                                  sMonSpriteCoords[sLinkBattleSelectScreen->cursorPosition][1] * 8, 2);
        gMain.state++;
        break;
    case 13:
        for (i = 0; i < sLinkBattleSelectScreen->partyCounts[LINK_PLAYER]; i++)
        {
            sLinkBattleSelectScreen->orderNumberSpriteIds[i] = CreateSprite(&sSpriteTemplate_OrderNumber, (sMonSpriteCoords[i][0] * 8) + 38, (sMonSpriteCoords[i][1] * 8) - 5, 1);

            if (sLinkBattleSelectScreen->selectedId[i] != 0)
                StartSpriteAnim(&gSprites[sLinkBattleSelectScreen->orderNumberSpriteIds[i]], sLinkBattleSelectScreen->selectedId[i] - 1);
            else
                gSprites[sLinkBattleSelectScreen->orderNumberSpriteIds[i]].invisible = TRUE;
        }

        gMain.state++;
        break;
    case 14:
        LoadLinkBattleSelectScreenBgGfx(0);
        gMain.state++;
        break;
    case 15:
        LoadLinkBattleSelectScreenBgGfx(1);
        SetActiveMenuOptions();
        gMain.state++;
        break;
    case 16:
        gPaletteFade->bufferTransferDisabled = FALSE;
        BlendPalettes(PALETTES_ALL, 16, RGB_BLACK);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        gMain.state++;
        break;
    case 17:
        SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_OBJ_1D_MAP | DISPCNT_OBJ_ON);
        LoadLinkBattleSelectScreenBgGfx(2);
        gMain.state++;
        break;
    case 18:
        if (!gPaletteFade->active)
            SetMainCallback2(CB2_LinkBattleSelectScreen);
        break;
    }

    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    UpdatePaletteFade();
}

static void CB_FadeToStartBattle(void)
{
    if (++sLinkBattleSelectScreen->timer >= 16)
    {
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
        sLinkBattleSelectScreen->callbackId = CB_WAIT_TO_START_BATTLE;
    }
}

static void CB_WaitToStartBattle(void)
{
    if (!gPaletteFade->active)
    {
        if (gWirelessCommType != 0)
        {
            sLinkBattleSelectScreen->callbackId = CB_WAIT_TO_START_RFU_BATTLE;
        }
        else
        {
            SetCloseLinkCallbackAndType(32);
            sLinkBattleSelectScreen->callbackId = CB_START_LINK_BATTLE;
        }
    }
}

static void Trade_Memcpy(void *dest, const void *src, size_t size)
{
    u32 i;
    u8 *_dest = dest;
    const u8 *_src = src;
    for (i = 0; i < size; i++)
        _dest[i] = _src[i];
}

static void CB_StartLinkBattle(void)
{
    switch (sLinkBattleSelectScreen->ReducePartyState)
    {
    case 0:
        if (sLinkBattleSelectScreen->selectedId[0] != 0)
            Trade_Memcpy(&sLinkBattleSelectScreen->selectedMon[sLinkBattleSelectScreen->selectedId[0] - 1], &gPlayerParty[0], sizeof(struct Pokemon));
        if (sLinkBattleSelectScreen->selectedId[1] != 0)
            Trade_Memcpy(&sLinkBattleSelectScreen->selectedMon[sLinkBattleSelectScreen->selectedId[1] - 1], &gPlayerParty[1], sizeof(struct Pokemon));
        sLinkBattleSelectScreen->ReducePartyState++;
        break;
    case 1:
        if (sLinkBattleSelectScreen->selectedId[2] != 0)
            Trade_Memcpy(&sLinkBattleSelectScreen->selectedMon[sLinkBattleSelectScreen->selectedId[2] - 1], &gPlayerParty[2], sizeof(struct Pokemon));
        if (sLinkBattleSelectScreen->selectedId[3] != 0)
            Trade_Memcpy(&sLinkBattleSelectScreen->selectedMon[sLinkBattleSelectScreen->selectedId[3] - 1], &gPlayerParty[3], sizeof(struct Pokemon));
        sLinkBattleSelectScreen->ReducePartyState++;
        break;
    case 2:
        if (sLinkBattleSelectScreen->selectedId[4] != 0)
            Trade_Memcpy(&sLinkBattleSelectScreen->selectedMon[sLinkBattleSelectScreen->selectedId[4] - 1], &gPlayerParty[4], sizeof(struct Pokemon));
        if (sLinkBattleSelectScreen->selectedId[5] != 0)
            Trade_Memcpy(&sLinkBattleSelectScreen->selectedMon[sLinkBattleSelectScreen->selectedId[5] - 1], &gPlayerParty[5], sizeof(struct Pokemon));
        sLinkBattleSelectScreen->ReducePartyState++;
        break;
    case 3:
        CpuFill32(0, gPlayerParty, 600);
        sLinkBattleSelectScreen->ReducePartyState++;
        break;
    case 4:
        Trade_Memcpy(&gPlayerParty[0], &sLinkBattleSelectScreen->selectedMon[0], 2 * sizeof(struct Pokemon));
        if (sLinkBattleSelectScreen->maxMon > 2)
            sLinkBattleSelectScreen->ReducePartyState++;
        else
            sLinkBattleSelectScreen->ReducePartyState = 7;
        break;
    case 5:
        Trade_Memcpy(&gPlayerParty[2], &sLinkBattleSelectScreen->selectedMon[2], 2 * sizeof(struct Pokemon));
        if (sLinkBattleSelectScreen->maxMon > 4)
            sLinkBattleSelectScreen->ReducePartyState++;
        else
            sLinkBattleSelectScreen->ReducePartyState = 7;
        break;
    case 6:
        Trade_Memcpy(&gPlayerParty[4], &sLinkBattleSelectScreen->selectedMon[4], 2 * sizeof(struct Pokemon));
        sLinkBattleSelectScreen->ReducePartyState++;
        break;
    case 7:
        CalculatePlayerPartyCount();
        sLinkBattleSelectScreen->ReducePartyState++;
        break;
    case 8:
        gLinkType = LINKTYPE_BATTLE;
        gTrainerBattleOpponent_A = TRAINER_LINK_OPPONENT;
        
        if (gLinkPlayers[0].trainerId & 1)
            PlayMapChosenOrBattleBGM(BGM_BATTLE_RSE_GYM_LEADER);
        else
            PlayMapChosenOrBattleBGM(BGM_BATTLE_RSE_TRAINER);
        switch (Var8004)
        {
        case USING_SINGLE_BATTLE:
            gBattleTypeFlags = BATTLE_TYPE_TRAINER | BATTLE_TYPE_LINK;
            break;
        case USING_DOUBLE_BATTLE:
            gBattleTypeFlags = BATTLE_TYPE_TRAINER | BATTLE_TYPE_LINK | BATTLE_TYPE_DOUBLE;
            break;
        case USING_MULTI_BATTLE:
            gBattleTypeFlags = BATTLE_TYPE_TRAINER | BATTLE_TYPE_LINK | BATTLE_TYPE_DOUBLE | BATTLE_TYPE_MULTI;
            break;
        }
        sLinkBattleSelectScreen->ReducePartyState++;
        break;
    case 9:
        gMain.savedCallback = CB2_ReturnFromCableClubBattle;
        
        if (gWirelessCommType != 0)
        {
            // Wireless
            if (IsLinkRfuTaskFinished())
            {
                FreeAllWindowBuffers();
                Free(sLinkBattleSelectScreen);
                gMain.callback1 = NULL;
                DestroyWirelessStatusIndicatorSprite();
                SetMainCallback2(CB2_InitBattle);
            }
        }
        else
        {
            // Cable
            if (!gReceivedRemoteLinkPlayers)
            {
                FreeAllWindowBuffers();
                Free(sLinkBattleSelectScreen);
                gMain.callback1 = NULL;
                SetMainCallback2(CB2_InitBattle);
            }
        }
    }
}

static void CB2_LinkBattleSelectScreen(void)
{
    RunLinkBattleSelectScreenCallback();
    DoQueuedActions();
    RunTextPrintersAndIsPrinter0Active();
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    UpdatePaletteFade();
}

static void LoadLinkBattleSelectScreenBgGfx(u8 state)
{
    switch (state)
    {
    case 0:
        LoadPalette((void*) 0x821D2B0, 0, 0x60);
        LoadBgTiles(3, (void*) 0x821D330, 0x1280, 0);
        CopyToBgTilemapBufferRect_ChangePalette(3, (void*) 0x821E5B0, 0, 0, 32, 20, 0);
        break;
    case 1:
        PrintMonBoxes(LINK_PLAYER);
        PrintMonBoxes(LINK_PARTNER);
        CopyBgTilemapBufferToVram(3);
        break;
    case 2:
        SetGpuReg(REG_OFFSET_BG3HOFS, DISPCNT_MODE_0);
        SetGpuReg(REG_OFFSET_BG3VOFS, DISPCNT_MODE_0);
        SetGpuReg(REG_OFFSET_BG2HOFS, DISPCNT_MODE_0);
        SetGpuReg(REG_OFFSET_BG2VOFS, DISPCNT_MODE_0);
        SetGpuReg(REG_OFFSET_BG1HOFS, DISPCNT_MODE_0);
        SetGpuReg(REG_OFFSET_BG1VOFS, DISPCNT_MODE_0);
        SetGpuReg(REG_OFFSET_BG0HOFS, DISPCNT_MODE_0);
        SetGpuReg(REG_OFFSET_BG0VOFS, DISPCNT_MODE_0);
        ShowBg(0);
        ShowBg(1);
        ShowBg(3);
        break;
    }
}

static void SetActiveMenuOptions(void)
{
    int i;
    for (i = 0; i < PARTY_SIZE; i++)
    {
        if (i < sLinkBattleSelectScreen->partyCounts[LINK_PLAYER])
        {
            gSprites[sLinkBattleSelectScreen->partySpriteIds[LINK_PLAYER][i]].invisible = FALSE;
            sLinkBattleSelectScreen->optionsActive[i] = TRUE;
        }
        else
        {
            sLinkBattleSelectScreen->optionsActive[i] = FALSE;
        }

        if (i < sLinkBattleSelectScreen->partyCounts[LINK_PARTNER])
        {
            gSprites[sLinkBattleSelectScreen->partySpriteIds[LINK_PARTNER][i]].invisible = FALSE;
        }
    }

    sLinkBattleSelectScreen->optionsActive[PARTY_SIZE] = TRUE;
}

static bool8 BufferLinkBattleParties(void)
{
    u8 id = GetMultiplayerId();

    switch (sLinkBattleSelectScreen->bufferPartyState)
    {
    case 0:
        Trade_Memcpy(gBlockSendBuffer, &gPlayerParty[0], 2 * sizeof(struct Pokemon));
        sLinkBattleSelectScreen->bufferPartyState++;
        sLinkBattleSelectScreen->timer = 0;
        break;
    case 1:
        if (IsLinkTaskFinished())
        {
            if (GetBlockReceivedStatus() == 0)
            {
                sLinkBattleSelectScreen->bufferPartyState++;
            }
            else
            {
                ResetBlockReceivedFlags();
                sLinkBattleSelectScreen->bufferPartyState++;
            }
        }
        break;
    case 3:
        if (id == 0)
            SendBlockRequest(BLOCK_REQ_SIZE_200);
        sLinkBattleSelectScreen->bufferPartyState++;
        break;
    case 4:
        if (GetBlockReceivedStatus() == 3)
        {
            Trade_Memcpy(&gEnemyParty[0], gBlockRecvBuffer[id ^ 1], 2 * sizeof(struct Pokemon));
            ResetBlockReceivedFlags();
            sLinkBattleSelectScreen->bufferPartyState++;
        }
        break;
    case 5:
        Trade_Memcpy(gBlockSendBuffer, &gPlayerParty[2], 2 * sizeof(struct Pokemon));
        sLinkBattleSelectScreen->bufferPartyState++;
        break;
    case 7:
        if (id == 0)
            SendBlockRequest(BLOCK_REQ_SIZE_200);
        sLinkBattleSelectScreen->bufferPartyState++;
        break;
    case 8:
        if (GetBlockReceivedStatus() == 3)
        {
            Trade_Memcpy(&gEnemyParty[2], gBlockRecvBuffer[id ^ 1], 2 * sizeof(struct Pokemon));
            ResetBlockReceivedFlags();
            sLinkBattleSelectScreen->bufferPartyState++;
        }
        break;
    case 9:
        Trade_Memcpy(gBlockSendBuffer, &gPlayerParty[4], 2 * sizeof(struct Pokemon));
        sLinkBattleSelectScreen->bufferPartyState++;
        break;
    case 11:
        if (id == 0)
            SendBlockRequest(BLOCK_REQ_SIZE_200);
        sLinkBattleSelectScreen->bufferPartyState++;
        break;
    case 12:
        if (GetBlockReceivedStatus() == 3)
        {
            Trade_Memcpy(&gEnemyParty[4], gBlockRecvBuffer[id ^ 1], 2 * sizeof(struct Pokemon));
            ResetBlockReceivedFlags();
            sLinkBattleSelectScreen->bufferPartyState++;
        }
        break;
    case 13:
        return TRUE;
    // Delay until next state
    case 2:
    case 6:
    case 10:
        sLinkBattleSelectScreen->timer++;
        if (sLinkBattleSelectScreen->timer > 10)
        {
            sLinkBattleSelectScreen->timer = 0;
            sLinkBattleSelectScreen->bufferPartyState++;
        }
        break;
    }
    return FALSE;
}

static void Leader_ReadLinkBuffer(u8 status)
{
    if (status & 1)
    {
        switch (gBlockRecvBuffer[0][0])
        {
        case LINKCMD_INIT_BLOCK:
            sLinkBattleSelectScreen->playerConfirmStatus = LINK_STATUS_READY;
            break;
        }
        ResetBlockReceivedFlag(0);
    }

    if (status & 2)
    {
        switch (gBlockRecvBuffer[1][0])
        {
        case LINKCMD_INIT_BLOCK:
            sLinkBattleSelectScreen->partnerConfirmStatus = LINK_STATUS_READY;
            break;
        }
        ResetBlockReceivedFlag(1);
    }
}

static void Follower_ReadLinkBuffer(u8 status)
{
    if (status & 1)
    {
        switch (gBlockRecvBuffer[0][0])
        {
        case LINKCMD_START_TRADE:
            BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
            sLinkBattleSelectScreen->callbackId = CB_WAIT_TO_START_BATTLE;
            break;
        }
        ResetBlockReceivedFlag(0);
    }

    if (status & 2)
        ResetBlockReceivedFlag(1);
}

#define QueueLinkData(linkCmd, cursorPosition) \
{ \
    sLinkBattleSelectScreen->linkData[0] = (linkCmd); \
    sLinkBattleSelectScreen->linkData[1] = (cursorPosition); \
    QueueAction(QUEUE_DELAY_DATA, QUEUE_SEND_DATA); \
}

static void Leader_HandleCommunication(void)
{
    if (sLinkBattleSelectScreen->playerConfirmStatus != LINK_STATUS_NONE
     && sLinkBattleSelectScreen->partnerConfirmStatus != LINK_STATUS_NONE)
    {
        if (sLinkBattleSelectScreen->playerConfirmStatus == LINK_STATUS_READY
         && sLinkBattleSelectScreen->partnerConfirmStatus == LINK_STATUS_READY)
        {
            QueueLinkData(LINKCMD_START_TRADE, 0);
            sLinkBattleSelectScreen->playerConfirmStatus = LINK_STATUS_NONE;
            sLinkBattleSelectScreen->partnerConfirmStatus = LINK_STATUS_NONE;
            sLinkBattleSelectScreen->callbackId = CB_FADE_TO_START_BATTLE;
        }
    }
}

static void CB1_UpdateLink(void)
{
    u8 mpId = GetMultiplayerId();
    u8 status;

    if ((status = GetBlockReceivedStatus()))
    {
        if (mpId == 0)
            Leader_ReadLinkBuffer(status);
        else
            Follower_ReadLinkBuffer(status);
    }
    if (mpId == 0)
        Leader_HandleCommunication();
}

static u8 GetNewCursorPosition(u8 oldPosition, u8 direction)
{
    u8 newPosition = 0;

    switch (direction)
    {
        case DIR_UP:
            switch (oldPosition)
            {
                case 0:
                case 1:
                    newPosition = 6;
                    break;
                case 6:
                    newPosition = sLinkBattleSelectScreen->partyCounts[LINK_PLAYER] - 1;
                    break;
                default:
                    newPosition = oldPosition - 2;
                    break;
            }
            break;
        case DIR_DOWN:
            switch (oldPosition)
            {
                case 4:
                case 5:
                    newPosition = 6;
                    break;
                case 6:
                    newPosition = 0;
                    break;
                default:
                    if (sLinkBattleSelectScreen->optionsActive[oldPosition + 2])
                        newPosition = oldPosition + 2;
                    else
                        newPosition = 6;
                    break;
            }
            break;
        case DIR_LEFT:
            switch (oldPosition)
            {
                case 0:
                case 2:
                case 4:
                case 6:
                    newPosition = oldPosition;
                    break;
                default:
                    newPosition = oldPosition - 1;
                    break;
            }
            break;
        case DIR_RIGHT:
            switch (oldPosition)
            {
                case 1:
                case 3:
                case 6:
                    newPosition = oldPosition;
                    break;
                case 5:
                    newPosition = oldPosition + 1;
                    break;
                default:
                    if (sLinkBattleSelectScreen->optionsActive[oldPosition + 1])
                        newPosition = oldPosition + 1;
                    else
                        newPosition = 6;
                    break;
            }
            break;
    }

    return newPosition;
}

static void SelectScreenMoveCursor(u8 *cursorPosition, u8 direction)
{
    u8 newPosition = GetNewCursorPosition(*cursorPosition, direction);

    if (newPosition == PARTY_SIZE)
    {
        StartSpriteAnim(&gSprites[sLinkBattleSelectScreen->cursorSpriteId], CURSOR_ANIM_ON_CANCEL);
        gSprites[sLinkBattleSelectScreen->cursorSpriteId].pos1.x = DISPLAY_WIDTH - 8;
        gSprites[sLinkBattleSelectScreen->cursorSpriteId].pos1.y = DISPLAY_HEIGHT;
    }
    else
    {
        StartSpriteAnim(&gSprites[sLinkBattleSelectScreen->cursorSpriteId], CURSOR_ANIM_NORMAL);
        gSprites[sLinkBattleSelectScreen->cursorSpriteId].pos1.x = sMonSpriteCoords[newPosition][0] * 8 + 32;
        gSprites[sLinkBattleSelectScreen->cursorSpriteId].pos1.y = sMonSpriteCoords[newPosition][1] * 8;
    }

    if (*cursorPosition != newPosition)
        PlaySE(SE_SELECT);

    *cursorPosition = newPosition;
}

static void SetReadyToLinkBattle(void)
{
    PrintMessageAndWindow(MSG_STANDBY);
    sLinkBattleSelectScreen->callbackId = CB_READY_WAIT;

    if (GetMultiplayerId() == 1)
    {
        sLinkBattleSelectScreen->linkData[0] = LINKCMD_INIT_BLOCK;
        SendBlock(BitmaskAllOtherLinkPlayers(), sLinkBattleSelectScreen->linkData, 20);
    }
    else
    {
        sLinkBattleSelectScreen->playerConfirmStatus = LINK_STATUS_READY;
    }
}

static void CB_ProcessMenuInput(void)
{
    if (JOY_REPT(DPAD_UP))
        SelectScreenMoveCursor(&sLinkBattleSelectScreen->cursorPosition, DIR_UP);
    else if (JOY_REPT(DPAD_DOWN))
        SelectScreenMoveCursor(&sLinkBattleSelectScreen->cursorPosition, DIR_DOWN);
    else if (JOY_REPT(DPAD_LEFT))
        SelectScreenMoveCursor(&sLinkBattleSelectScreen->cursorPosition, DIR_LEFT);
    else if (JOY_REPT(DPAD_RIGHT))
        SelectScreenMoveCursor(&sLinkBattleSelectScreen->cursorPosition, DIR_RIGHT);

    if (JOY_NEW(A_BUTTON))
    {
        if (sLinkBattleSelectScreen->cursorPosition < PARTY_SIZE)
        {
            PlaySE(SE_SELECT);
            DrawTextBorderOuter(LS_WIN_OPTIONS, 1, 14);
            FillWindowPixelBuffer(LS_WIN_OPTIONS, PIXEL_FILL(1));
            if (sLinkBattleSelectScreen->selectedId[sLinkBattleSelectScreen->cursorPosition] != 0)
                AddTextPrinterParameterized(LS_WIN_OPTIONS, 2, gText_Deselect, 8, 2, 0xFF, NULL);
            else
                AddTextPrinterParameterized(LS_WIN_OPTIONS, 2, gText_Select, 8, 2, 0xFF, NULL);

            AddTextPrinterParameterized(LS_WIN_OPTIONS, 2, gText_Summary, 8, 16, 0xFF, NULL);
            Menu_InitCursor(LS_WIN_OPTIONS, 2, 0, 0, 16, 2, 0);
            PutWindowTilemap(LS_WIN_OPTIONS);
            CopyWindowToVram(LS_WIN_OPTIONS, COPYWIN_BOTH);
            sLinkBattleSelectScreen->callbackId = CB_SELECTED_MON;
        }
        else if (sLinkBattleSelectScreen->cursorPosition == PARTY_SIZE)
        {
            if (sLinkBattleSelectScreen->playerSelectStatus == sLinkBattleSelectScreen->maxMon
            || sLinkBattleSelectScreen->playerSelectStatus == sLinkBattleSelectScreen->partyCounts[LINK_PLAYER])
            {
                PlaySE(SE_SELECT);
                SetReadyToLinkBattle();
            }
            else
            {
                PlaySE(SE_ERROR);
            }
        }
    }
}

static void ClearMsgWindow(u8 windowId)
{
    ClearStdWindowAndFrameToTransparent(windowId, TRUE);
    ClearWindowTilemap(windowId);
}

static void RedrawChooseAPokemonWindow(void)
{
    ClearMsgWindow(LS_WIN_OPTIONS);
    sLinkBattleSelectScreen->callbackId = CB_MAIN_MENU;
}

void SetSelectedId(bool8 Selected)
{
    int i, j;
    u8 position = sLinkBattleSelectScreen->cursorPosition;

    if (Selected)
    {
        sLinkBattleSelectScreen->playerSelectStatus++;
        sLinkBattleSelectScreen->selectedId[position] = sLinkBattleSelectScreen->playerSelectStatus;
        StartSpriteAnim(&gSprites[sLinkBattleSelectScreen->orderNumberSpriteIds[position]], sLinkBattleSelectScreen->selectedId[position] - 1);
        gSprites[sLinkBattleSelectScreen->orderNumberSpriteIds[position]].invisible = FALSE;
    }
    else
    {
        for (i = sLinkBattleSelectScreen->selectedId[position]; i < sLinkBattleSelectScreen->maxMon + 1; ++i)
        {
            for (j = 0; j < sLinkBattleSelectScreen->partyCounts[LINK_PLAYER]; ++j)
            {
                if (sLinkBattleSelectScreen->selectedId[j] == i + 1)
                {
                    sLinkBattleSelectScreen->selectedId[j]--;
                    StartSpriteAnim(&gSprites[sLinkBattleSelectScreen->orderNumberSpriteIds[j]], sLinkBattleSelectScreen->selectedId[j] - 1);
                }
            }
        }
        sLinkBattleSelectScreen->selectedId[position] = 0;
        gSprites[sLinkBattleSelectScreen->orderNumberSpriteIds[position]].invisible = TRUE;
        sLinkBattleSelectScreen->playerSelectStatus--;
    }
}


static void CB_ProcessSelectedMonInput(void)
{
    switch (RboxChoiceUpdate())
    {
    case MENU_B_PRESSED:
        PlaySE(SE_SELECT);
        RedrawChooseAPokemonWindow();
        break;
    case 0:
        if (sLinkBattleSelectScreen->selectedId[sLinkBattleSelectScreen->cursorPosition] != 0)
        {
            SetSelectedId(FALSE);
            RedrawChooseAPokemonWindow();
        }
        else if (sLinkBattleSelectScreen->playerSelectStatus < sLinkBattleSelectScreen->maxMon)
        {
            SetSelectedId(TRUE);
            RedrawChooseAPokemonWindow();
        }
        else
        {
            QueueAction(QUEUE_DELAY_MSG, QUEUE_MON_CANT_BE_SELECT);
            sLinkBattleSelectScreen->callbackId = CB_CLEAR_MSG;
        }
        break;
    case 1: // Summary
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
        sLinkBattleSelectScreen->callbackId = CB_SHOW_MON_SUMMARY;
        break;
    }
}

static void CB_ShowBattleMonSummaryScreen(void)
{
    if (!gPaletteFade->active)
    {
        ShowPokemonSummaryScreen(gPlayerParty, sLinkBattleSelectScreen->cursorPosition, sLinkBattleSelectScreen->partyCounts[0] - 1, CB2_ReturnToLinkBattleSelectScreenFromSummary, PSS_MODE_TRADE);
        FreeAllWindowBuffers();
    }
}

static void CB_HandleMessageWindow(void)
{
    if (JOY_NEW(A_BUTTON))
    {
        PlaySE(SE_SELECT);
        ClearMsgWindow(LS_WIN_MSG);
        ClearMsgWindow(LS_WIN_OPTIONS);
        sLinkBattleSelectScreen->callbackId = CB_MAIN_MENU;
    }
}

static void CB_WaitToStartRfuBattle(void)
{
    if (!Rfu_SetLinkRecovery(FALSE))
    {
        SetLinkStandbyCallback();
        sLinkBattleSelectScreen->callbackId = CB_START_LINK_BATTLE;
    }
}

static void RunLinkBattleSelectScreenCallback(void)
{
    switch (sLinkBattleSelectScreen->callbackId)
    {
    case CB_MAIN_MENU:
        CB_ProcessMenuInput();
        break;
    case CB_SELECTED_MON:
        CB_ProcessSelectedMonInput();
        break;
    case CB_SHOW_MON_SUMMARY:
        CB_ShowBattleMonSummaryScreen();
        break;
    case CB_READY_WAIT:
        // nop
        break;
    case CB_CLEAR_MSG:
        CB_HandleMessageWindow();
        break;
    case CB_FADE_TO_START_BATTLE:
        CB_FadeToStartBattle();
        break;
    case CB_WAIT_TO_START_BATTLE:
        CB_WaitToStartBattle();
        break;
    case CB_START_LINK_BATTLE:
        CB_StartLinkBattle();
        break;
    case CB_WAIT_TO_START_RFU_BATTLE:
        CB_WaitToStartRfuBattle();
        break;
    }
    // CB_IDLE is nop
}

static void PrintMonBox(u8 winLeft, u8 winTop, u8 x, u8 y)
{
    CopyToBgTilemapBufferRect_ChangePalette(3, (void*) 0x82201AC, winLeft, winTop, 6, 3, 0);
    CopyBgTilemapBufferToVram(3);
    sLinkBattleSelectScreen->tilemapBuffer[x + (y * 32) - 32] = sLinkBattleSelectScreen->tilemapBuffer[x + (y * 32) - 33];
    sLinkBattleSelectScreen->tilemapBuffer[x + (y * 32) - 31] = sLinkBattleSelectScreen->tilemapBuffer[x + (y * 32) - 36] | 0x400;
}

static void PrintMonBoxes(u8 whichParty)
{
    s32 i;
    for (i = 0; i < sLinkBattleSelectScreen->partyCounts[whichParty]; i++)
    {
        s32 j = i + PARTY_SIZE * whichParty;
        PrintMonBox(sMonBoxCoords[j][0], sMonBoxCoords[j][1], sMonLevelCoords[j][0], sMonLevelCoords[j][1]);
    }
}

static void PrintLevelAndGender(u8 whichParty, u8 windowId, u8 level, u8 gender)
{
    u8 buff[20];
    u8 *stringPtr;

    stringPtr = StringCopy(buff, gText_Level);
    ConvertIntToDecimalStringN(stringPtr, level, STR_CONV_MODE_LEFT_ALIGN, 3);
    windowId += (whichParty * PARTY_SIZE) + 2;
    WindowPrint(windowId, 0, 4, 0, &sTextColor_PartyMonNickname, 0, buff);
    if (gender == MON_FEMALE)
        WindowPrint(windowId, 0, 38, 0, &sFemaleColors, 0xFF, gText_FemaleSymbol);
    else if (gender == MON_MALE)
        WindowPrint(windowId, 0, 38, 0, &sMaleColors, 0xFF, gText_MaleSymbol);
    PutWindowTilemap(windowId);
    CopyWindowToVram(windowId, COPYWIN_BOTH);
}

static void PrintPartyLevelsAndGenders(u8 whichParty)
{
    u8 level, gender;
    struct Pokemon * party = (whichParty == LINK_PLAYER) ? gPlayerParty : gEnemyParty;
    u8 i;
    for (i = 0; i < sLinkBattleSelectScreen->partyCounts[whichParty]; i++)
    {
        level = GetMonData(&party[i], MON_DATA_LEVEL, NULL);
        gender = GetMonGender(&party[i]);
        PrintLevelAndGender(whichParty, i, level, gender);
    }
}

static void PrintPlayerName(u8 whichParty)
{
    u8 windowId = (whichParty == LINK_PLAYER) ? LS_WIN_PLAYER_NAME : LS_WIN_OPPONENT_NAME;
    u8 *name = (whichParty == LINK_PLAYER) ? gSaveBlock2->playerName : gLinkPlayers[GetMultiplayerId() ^ 1].name;
    u32 xPos = (56 - GetStringWidth(1, name, 0)) / 2;

    if (whichParty == LINK_PARTNER)
        WindowPrint(windowId, 1, xPos, 4, &sTextColor_PartyMonNickname, 0xFF, gLinkPlayers[GetMultiplayerId() ^ 1].name);
    else
        WindowPrint(windowId, 1, xPos, 4, &sTextColor_PartyMonNickname, 0xFF, gSaveBlock2->playerName);
    
    PutWindowTilemap(windowId);
    CopyWindowToVram(windowId, COPYWIN_BOTH);
}

static void PrintConfirm(void)
{
    FillWindowPixelBuffer(LS_WIN_CONFIRM, PIXEL_FILL(0));
    AddTextPrinterParameterized4(LS_WIN_CONFIRM, 2, 5, 1, 0, 0, &sTextColor_PartyMonNickname, 0xFF, gText_confirm);
    PutWindowTilemap(LS_WIN_CONFIRM);
    CopyWindowToVram(LS_WIN_CONFIRM, COPYWIN_BOTH);
}

static void QueueAction(u16 delay, u8 actionId)
{
    int i;
    for (i = 0; i < (int)ARRAY_COUNT(sLinkBattleSelectScreen->queuedActions); i++)
    {
        // Find first available spot
        if (!sLinkBattleSelectScreen->queuedActions[i].active)
        {
            sLinkBattleSelectScreen->queuedActions[i].delay = delay;
            sLinkBattleSelectScreen->queuedActions[i].actionId = actionId;
            sLinkBattleSelectScreen->queuedActions[i].active = TRUE;
            break;
        }
    }
}

static void DoQueuedActions(void)
{
    int i;

    for (i = 0; i < (int)ARRAY_COUNT(sLinkBattleSelectScreen->queuedActions); i++)
    {
        if (sLinkBattleSelectScreen->queuedActions[i].active)
        {
            if (sLinkBattleSelectScreen->queuedActions[i].delay != 0)
            {
                sLinkBattleSelectScreen->queuedActions[i].delay--;
            }
            else
            {
                switch (sLinkBattleSelectScreen->queuedActions[i].actionId)
                {
                case QUEUE_SEND_DATA:
                    SendBlock(BitmaskAllOtherLinkPlayers(), sLinkBattleSelectScreen->linkData, 20);
                    break;
                case QUEUE_STANDBY:
                    PrintMessageAndWindow(MSG_STANDBY);
                    break;
                case QUEUE_MON_CANT_BE_SELECT:
                    PrintMessageAndWindow(MSG_MON_CANT_BE_SELECT);
                    break;
                }
                sLinkBattleSelectScreen->queuedActions[i].active = FALSE;
            }
        }
    }
}

static void PrintMessageAndWindow(u8 messageId)
{
    FillWindowPixelBuffer(0, PIXEL_FILL(1));
    AddTextPrinterParameterized(0, 2, sMessages[messageId], 0, 2, 0xFF, NULL);
    DrawTextBorderOuter(0, 0x014, 12);
    PutWindowTilemap(0);
    CopyWindowToVram(0, COPYWIN_BOTH);
}

static bool8 LoadUISpriteGfx(void)
{
    switch (sLinkBattleSelectScreen->timer)
    {
    case 0:
        LoadSpritePalette(&sCursor_SpritePalette);
        sLinkBattleSelectScreen->timer++;
        break;
    case 1:
        LoadSpriteSheet(&sCursor_SpriteSheet);
        sLinkBattleSelectScreen->timer++;
        break;
    case 2:
        LoadSpritePalette(&sOrderNumber_SpritePalette);
        sLinkBattleSelectScreen->timer++;
        break;
    case 3:
        LoadSpriteSheet(&sOrderNumber_SpriteSheet);
        sLinkBattleSelectScreen->timer++;
        break;
    case 4:
        sLinkBattleSelectScreen->timer = 0;
        return TRUE;
    }

    return FALSE;
}

static void DrawBottomRowText(const u8 *text)
{
    FillWindowPixelBuffer(LS_WIN_BOTTOM_TEXT, PIXEL_FILL(0));
    AddTextPrinterParameterized4(LS_WIN_BOTTOM_TEXT, 2, 0, 1, 0, 0, &sTextColor_PartyMonNickname, 0, text);
    PutWindowTilemap(LS_WIN_BOTTOM_TEXT);
    CopyWindowToVram(LS_WIN_BOTTOM_TEXT, COPYWIN_BOTH);
}

void CB2_StartCreateLinkBattleSelectScreen(void)
{
    SetMainCallback2(CB2_CreateLinkBattleSelectScreen);
}

static void Task_CreateLinkBattleSelectScreen(u8 taskId)
{
    CB2_StartCreateLinkBattleSelectScreen();
    DestroyTask(taskId);
}

u8 CreateTask_CreateLinkBattleSelectScreen(void)
{
    return CreateTask(Task_CreateLinkBattleSelectScreen, 0);
}

#define tState      data[0]
void Task_StartWiredLinkBattleSelectScreen(u8 taskId)
{
    struct Task *task = &gTasks[taskId];
    switch (task->tState)
    {
    case 0:
        LockPlayerFieldControls();
        FadeScreen(FADE_TO_BLACK, 0);
        ClearLinkCallback_2();
        task->tState++;
        break;
    case 1:
        if (!gPaletteFade->active)
            task->tState++;
        break;
    case 2:
        SetCloseLinkCallback();
        task->tState++;
        break;
    case 3:
        if (!gReceivedRemoteLinkPlayers)
        {
            SetMainCallback2(CB2_StartCreateLinkBattleSelectScreen);
            DestroyTask(taskId);
        }
        break;
    }
}

void Task_StartWirelessLinkBattleSelectScreen(u8 taskId)
{
    s16 *data = gTasks[taskId].data;
    switch (tState)
    {
    case 0:
        LockPlayerFieldControls();
        FadeScreen(FADE_TO_BLACK, 0);
        ClearLinkRfuCallback();
        tState++;
        break;
    case 1:
        if (!gPaletteFade->active)
            tState++;
        break;
    case 2:
        SetLinkStandbyCallback();
        tState++;
        break;
    case 3:
        if (IsLinkTaskFinished())
        {
            CreateTask_CreateLinkBattleSelectScreen();
            DestroyTask(taskId);
        }
        break;
    }
}

void EnterColosseumPlayerSpot(void)
{
    if (gWirelessCommType)
    {
        if (Var8004 == USING_MULTI_BATTLE)
        {
            gLinkType = LINKTYPE_BATTLE;
            CreateTask_EnterCableClubSeat(Task_StartWirelessCableClubBattle);
        }
        else
        {
            CreateTask_EnterCableClubSeat(Task_StartWirelessLinkBattleSelectScreen);
        }
    }
    else
    {
        if (Var8004 == USING_MULTI_BATTLE)
        {
            gLinkType = LINKTYPE_BATTLE;
            CreateTask_EnterCableClubSeat(Task_StartWiredCableClubBattle);
        }
        else
        {
            CreateTask_EnterCableClubSeat(Task_StartWiredLinkBattleSelectScreen);
        }
    }
}
