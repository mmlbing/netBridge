/*¹Ì¶¨ÊýÖµ*/
#define PAK_QUEUE_LENGTH	100
//#define	PAK_NODE_SIZE		1533


/*ÃüÁî*/
#define	SET_REQ_COMD		0x51
#define	SET_REQ_STATE		0x52
#define	SET_REQ_READ_START	0x53
#define	SET_REQ_READ_FINISH	0x54
#define	SET_REQ_READ_ERR	0x55


#define	SET_VAL_RESET		0x0001
#define	SET_VAL_START		0x0002
#define	SET_VAL_STOP		0x0004
#define	SET_VAL_MODE_CR		0x0010
#define	SET_VAL_MODE_CF		0x0020
#define	CMD_CLEAR_MASK		0xC000
#define	MODE_CR				0x8000
#define	MODE_CF				0x4000


#define	SET_VAL_GET_QUEUE_STATE		0x0001

#define	GET_QUEUE_STATE_RES_LENGTH		PAK_QUEUE_LENGTH*2+2


