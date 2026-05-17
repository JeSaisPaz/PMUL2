const { prisma, ORDER_STATUS, ITEM_STATUS, DECISION, TEAM } = require("../routes/adapter");

async function getScans() {
    return prisma.rEAD_CYCLE.findMany({
        include: { ITEM: { include: { COLOR: true } } },
        orderBy: { scannedAt: 'desc' }
    });
}

async function deleteScan(id) {
    const scan = await prisma.rEAD_CYCLE.findUnique({ where: { id } });
    if (!scan) throw { code: 404, message: "Scan not found" };
    await prisma.rEAD_CYCLE.delete({ where: { id } });
}

async function createScan(scan) {
    const readCycle = await prisma.rEAD_CYCLE.create({
        data: {
            qrValue: scan.qrValue,
            hue: scan.hue,
            saturation: scan.saturation,
            value: scan.value
        }
    });

    const color = await prisma.cOLOR.findFirst({
        where: {
            hueMin: { lte: scan.hue }, hueMax: { gte: scan.hue },
            saturationMin: { lte: scan.saturation }, saturationMax: { gte: scan.saturation },
            valueMin: { lte: scan.value }, valueMax: { gte: scan.value },
        }
    });

    const validColor = (color && color.status === true) ? color : null;

    if (scan.qrValue === TEAM.TEAM01) {
        //on recupere toutes les lignes de commandes contenant la couleur et en incluant les items ordered ou in process
        let orderLineInNeed = null;
        if(validColor){
            const orderLines = await prisma.oRDER_LINE.findMany({
                where: {
                    COLOR_id: validColor.id,
                    ORDER: { status: ORDER_STATUS.IN_PROCESS },
                    status: ORDER_STATUS.IN_PROCESS,
                },
                orderBy: { ORDER: { createdAt: 'asc' } },
                include: {
                    ITEM: {
                        where: { status: { in: [ITEM_STATUS.ORDERED, ITEM_STATUS.IN_PROCESS] } }
                    }
                }
            });
            orderLineInNeed = orderLines.find(line => line.ITEM.length < line.quantity) ?? null;
        }
        //on prend la commande la plus vielle ('asc') avec de la place
        const newItem = await prisma.iTEM.create({
            data: {
                team: scan.qrValue,
                decision: orderLineInNeed ? DECISION.ORDER : DECISION.STOCK,
                COLOR_id: color ? color.id : null,
                READ_CYCLE_id: readCycle.id,
                ORDER_LINE_id: orderLineInNeed ? orderLineInNeed.id : null,
                ITEM_HISTORY: { create: {} },
                SELECTION_HISTORY: { create: {} }
            }
        });
        return{
            itemId: newItem.id,
            decision: newItem.decision,
            orderId: orderLineInNeed ? orderLineInNeed.ORDER_id : null
        }
    } else {
        const validTeam = Object.values(TEAM).includes(scan.qrValue);
        const newItem = await prisma.iTEM.create({
            data: {
                team: validTeam ? scan.qrValue : null,
                decision: DECISION.PASS,
                COLOR_id: color ? color.id : null,
                READ_CYCLE_id: readCycle.id,
                ITEM_HISTORY: { create: {} },
                SELECTION_HISTORY: { create: {} }
            }
        });
        return {
            itemId: newItem.id,
            decision: newItem.decision,
            orderId: null
        };
    }
}

module.exports = { getScans, deleteScan, createScan };
