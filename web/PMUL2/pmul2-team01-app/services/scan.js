const { prisma, ORDER_STATUS, ITEM_STATUS, DECISION} = require("../routes/adapter");
const { incrementColorStat } = require("../services/stats");

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
    let validColor = null;
    if(color){
        incrementColorStat(color);
        validColor = (color.status === true) ? color : null;
    }

    if (scan.qrValue === "TEAM 01") {
        //On récupère la commande la plus vieille en PROCESS
        let orderLineInNeed = null;

        const oldestOrder = await prisma.oRDER.findFirst({
            where: { status: ORDER_STATUS.PROCESS },
            orderBy: { createdAt: 'asc' }
        });

        if (oldestOrder && validColor) {
            //On cherche une ligne de commande dans cette commande qui demande la couleur scannée
            const orderLine = await prisma.oRDER_LINE.findFirst({
                where: {
                    ORDER_id: oldestOrder.id,
                    COLOR_id: validColor.id,
                    status: ORDER_STATUS.PROCESS,
                },
                include: {
                    ITEM: {
                        where: { status: { in: [ITEM_STATUS.ORDERED, ITEM_STATUS.PROCESS] } }
                    }
                }
            });

            // On vérifie qu'il reste de la place dans cette ligne
            if (orderLine && orderLine.ITEM.length < orderLine.quantity) {
                orderLineInNeed = orderLine;
            }
        }

        const newItem = await prisma.iTEM.create({
            data: {
                team: scan.qrValue,
                decision: orderLineInNeed ? DECISION.ORDER : DECISION.STOCK,
                COLOR_id: color ? color.id : null,
                READ_CYCLE_id: readCycle.id,
                ORDER_LINE_id: orderLineInNeed ? orderLineInNeed.id : null,
                SELECTION_HISTORY: { create: {} }
            }
        });
        return{
            itemId: newItem.id,
            decision: newItem.decision,
            orderId: orderLineInNeed ? orderLineInNeed.ORDER_id : null,
            team: newItem.team,
            color: color ? color.name : null
        }
    } else {
        const TEAMS = ["TEAM 01", "TEAM 02", "TEAM 03", "TEAM 04", "TEAM 05"];
        const validTeam = TEAMS.includes(scan.qrValue);
        const newItem = await prisma.iTEM.create({
            data: {
                team: validTeam ? scan.qrValue : null,
                decision: DECISION.PASS,
                COLOR_id: color ? color.id : null,
                READ_CYCLE_id: readCycle.id,
                SELECTION_HISTORY: { create: {} }
            }
        });
        return {
            itemId: newItem.id,
            decision: newItem.decision,
            orderId: null,
            team: validTeam ? newItem.team : null,
            color: color ? color.name : null
        };
    }
}

module.exports = { getScans, deleteScan, createScan };
