const { prisma, ITEM_STATUS, DECISION, DECISION_STATUS, ORDER_STATUS } = require("../routes/adapter");

async function getItems() {
    return prisma.iTEM.findMany({
        include: {
            COLOR: { select: { name: true, hex: true } },
            READ_CYCLE: { select: { scannedAt: true } },
        }
    });
}

async function getItemDetails(id) {
    const item = await prisma.iTEM.findUnique({
        where: { id },
        include: {
            COLOR: { select: { name: true, hex: true } },
            READ_CYCLE: { select: { scannedAt: true, qrValue: true, hue: true, saturation: true, value: true } },
            ORDER_LINE: { select: { id: true, ORDER: { select: { id: true, status: true } } } },
            SELECTION_HISTORY: { orderBy: { createdAt: 'desc' } }
        }
    });

    if (!item) throw { code: 404, message: "Item not found" };
    return item;
}

async function getLogs() {
    return prisma.sELECTION_HISTORY.findMany({
        orderBy: { createdAt: 'desc' },
        include: { ITEM: { select: { id: true, decision: true } } }
    });
}

async function deleteLog(id) {
    const log = await prisma.sELECTION_HISTORY.findUnique({ where: { id } });
    if (!log) throw { code: 404, message: "Log not found" };
    await prisma.sELECTION_HISTORY.delete({ where: { id } });
}

async function clearLogs() {
    const fiveMinutesAgo = new Date(Date.now() - 5 * 60 * 1000); //retourne le temps en ms, on soustrait 5 min en ms(5 min * 60 s * 1000 ms).
    const deleted = await prisma.readCycle.deleteMany({
        where: {
            createdAt: {
                lt: fiveMinutesAgo,//on delete les logs où createdAt est less than now - 5min
            },
        },
    });
}

async function deleteItem(id) {
    const item = await prisma.iTEM.findUnique({
        where: { id },
        include: { ORDER_LINE: { select: { status: true } } }
    });

    if (!item) throw { code: 404, message: "Item not found" };

    if (
        item.status === ITEM_STATUS.PROCESS ||
        item.ORDER_LINE?.status === ORDER_STATUS.PROCESS ||
        item.ORDER_LINE?.status === ORDER_STATUS.COMPLETED
    ) {
        throw { code: 400, message: "Item cannot be deleted" };
    }

    await prisma.iTEM.delete({ where: { id } });
}

async function updateItemStatus(id, status) {
    const item = await prisma.iTEM.findUnique({
        where: { id },
        include: { ORDER_LINE: { include: { ORDER: true } } }
    });

    if (!item) throw { code: 404, message: "Item not found" };
    if (!Object.values(DECISION_STATUS).includes(status.status)) {
        throw { code: 400, message: "Invalid status" };
    }
    if (item.decisionStatus !== DECISION_STATUS.PROCESS || item.status !== ITEM_STATUS.PROCESS) {
        throw { code: 400, message: "Item is not in a processable state" };
    }

    if (item.decision === DECISION.PASS) {
        const newStatus = status.status === DECISION_STATUS.CONFIRMED ? ITEM_STATUS.EXTERNAL : ITEM_STATUS.CANCELLED;
        await prisma.$transaction([
            prisma.iTEM.update({ where: { id }, data: { status: newStatus, decisionStatus: status.status } }),
            prisma.sELECTION_HISTORY.create({ data: { ITEM_id: id, decisionStatus: status.status, itemStatus: newStatus } })
        ]);
    }

    else if (item.decision === DECISION.STOCK) {
        const newStatus = status.status === DECISION_STATUS.CONFIRMED ? ITEM_STATUS.AVAILABLE : ITEM_STATUS.CANCELLED;
        await prisma.$transaction([
            prisma.iTEM.update({ where: { id }, data: { status: newStatus, decisionStatus: status.status } }),
            prisma.sELECTION_HISTORY.create({ data: { ITEM_id: id, decisionStatus: status.status, itemStatus: newStatus } })
        ]);
    }

    else if (item.decision === DECISION.ORDER) {
        const newStatus = status.status === DECISION_STATUS.CONFIRMED ? ITEM_STATUS.ORDERED : ITEM_STATUS.CANCELLED;

        if (item.ORDER_LINE?.ORDER.status !== ORDER_STATUS.CANCELLED) {
            await prisma.$transaction([
                prisma.iTEM.update({ where: { id }, data: { status: newStatus, decisionStatus: status.status } }),
                prisma.sELECTION_HISTORY.create({ data: { ITEM_id: id, decisionStatus: status.status, itemStatus: newStatus } })
            ]);

            const currentLine = await prisma.oRDER_LINE.findUnique({
                where: { id: item.ORDER_LINE_id },
                include: {
                    ITEM: { where: { status: ITEM_STATUS.ORDERED } },
                    ORDER: true
                }
            });

            if (currentLine.ITEM.length >= currentLine.quantity) {
                await prisma.oRDER_LINE.update({
                    where: { id: currentLine.id },
                    data: { status: ORDER_STATUS.COMPLETED }
                });

                const pendingLines = await prisma.oRDER_LINE.count({
                    where: { ORDER_id: currentLine.ORDER_id, status: ORDER_STATUS.PROCESS }
                });

                if (pendingLines === 0) {
                    await prisma.oRDER.update({
                        where: { id: currentLine.ORDER_id },
                        data: { status: ORDER_STATUS.COMPLETED, completedAt: new Date() }
                    });

                    const completedOrdersCount = await prisma.oRDER.count({
                        where: { status: ORDER_STATUS.COMPLETED }
                    });

                    return { completedOrdersCount };
                }
            }
        } else {
            await prisma.$transaction([
                prisma.iTEM.update({ where: { id }, data: { status: newStatus, decisionStatus: status.status } }),
                prisma.sELECTION_HISTORY.create({ data: { ITEM_id: id, decisionStatus: status.status, itemStatus: newStatus } })
            ]);
        }
    }
}

module.exports = { getItems, deleteItem, updateItemStatus, getLogs, deleteLog, clearLogs, getItemDetails };