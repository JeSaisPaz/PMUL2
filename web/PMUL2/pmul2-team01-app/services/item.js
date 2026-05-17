const { prisma, DECISION } = require("../routes/adapter");

async function getItems() {
    return prisma.iTEM.findMany({
        include: {
            COLOR: { select: { name: true, hex: true } },
            READ_CYCLE: { select: { scannedAt: true } },
        }
    });
}

async function deleteItem(id) {
    const item = await prisma.iTEM.findUnique({
        where: { id },
        include: { ORDER_LINE: { select: { status: true } } }
    });

    if (!item) throw { code: 404, message: "Item not found" };

    if (
        item.status === "IN_PROCESS" ||
        item.ORDER_LINE?.status === "IN_PROCESS" ||
        item.ORDER_LINE?.status === "COMPLETED"
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
    if (!["CONFIRMED", "FAILED", "IN_PROCESS"].includes(status.status)) {
        throw { code: 400, message: "Invalid status" };
    }
    if (item.decisionStatus !== "IN_PROCESS" || item.status !== "IN_PROCESS") {
        throw { code: 400, message: "Item is not in a processable state" };
    }

    if (item.decision === DECISION.PASS) {
        const newStatus = status.status === "CONFIRMED" ? "EXTERNAL" : "CANCELLED";
        await prisma.$transaction([
            prisma.iTEM.update({ where: { id }, data: { status: newStatus, decisionStatus: status.status } }),
            prisma.iTEM_HISTORY.create({ data: { ITEM_id: id, status: newStatus } }),
            prisma.sELECTION_HISTORY.create({ data: { ITEM_id: id, status: status.status } })
        ]);
    }

    else if (item.decision === DECISION.STOCK) {
        const newStatus = status.status === "CONFIRMED" ? "AVAILABLE" : "CANCELLED";
        await prisma.$transaction([
            prisma.iTEM.update({ where: { id }, data: { status: newStatus, decisionStatus: status.status } }),
            prisma.iTEM_HISTORY.create({ data: { ITEM_id: id, status: newStatus } }),
            prisma.sELECTION_HISTORY.create({ data: { ITEM_id: id, status: status.status } })
        ]);
    }

    else if (item.decision === DECISION.ORDER) {
        const newStatus = status.status === "CONFIRMED" ? "ORDERED" : "CANCELLED";

        if (item.ORDER_LINE?.ORDER.status !== "CANCELLED") {
            await prisma.$transaction([
                prisma.iTEM.update({ where: { id }, data: { status: newStatus, decisionStatus: status.status } }),
                prisma.iTEM_HISTORY.create({ data: { ITEM_id: id, status: newStatus } }),
                prisma.sELECTION_HISTORY.create({ data: { ITEM_id: id, status: status.status } })
            ]);

            const currentLine = await prisma.oRDER_LINE.findUnique({
                where: { id: item.ORDER_LINE_id },
                include: {
                    ITEM: { where: { status: "ORDERED" } },
                    ORDER: true
                }
            });

            if (currentLine.ITEM.length >= currentLine.quantity) {
                await prisma.oRDER_LINE.update({
                    where: { id: currentLine.id },
                    data: { status: "COMPLETED" }
                });

                const pendingLines = await prisma.oRDER_LINE.count({
                    where: { ORDER_id: currentLine.ORDER_id, status: "IN_PROCESS" }
                });

                if (pendingLines === 0) {
                    await prisma.oRDER.update({
                        where: { id: currentLine.ORDER_id },
                        data: { status: "COMPLETED", completedAt: new Date() }
                    });
                }
            }
        } else {
            await prisma.$transaction([
                prisma.iTEM.update({ where: { id }, data: { status: newStatus, decisionStatus: status.status } }),
                prisma.sELECTION_HISTORY.create({ data: { ITEM_id: id, status: status.status } })
            ]);
        }
    }
}

module.exports = { getItems, deleteItem, updateItemStatus };
