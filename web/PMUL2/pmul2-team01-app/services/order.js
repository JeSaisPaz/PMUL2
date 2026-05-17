const { prisma } = require("../routes/adapter");

async function getOrders() {
    return prisma.oRDER.findMany({
        orderBy: { createdAt: 'desc' }
    });
}

async function getOrderDetails(id) {
    const order = await prisma.oRDER.findUnique({
        where: { id },
        include: {
            ORDER_LINE: { include: { COLOR: { select: { name: true, hex: true } }, ITEM: { select: { id: true, status: true }}}}
        }
    });

    if (!order) throw { code: 404, message: "Order not found" };

    return {
        ...order,
        ORDER_LINE: order.ORDER_LINE.map(line => ({
            ...line,
            orderedCount:   line.ITEM.filter(i => i.status === "ORDERED").length,
            inProcessCount: line.ITEM.filter(i => i.status === "IN_PROCESS").length,
            cancelledCount: line.ITEM.filter(i => i.status === "CANCELLED").length,
        }))
    };
}

async function createOrder(lines) {
    if (!Array.isArray(lines) || lines.length === 0) throw { code: 400, message: "Order must contain at least one line." };

    const colorIds = lines.map(color => color.id);
    const hasInactive = await prisma.cOLOR.findFirst({ where: { id: { in: colorIds }, status: false }});
    if (hasInactive) throw { code: 400, message: "Inactive color detected." };

    return prisma.oRDER.create({
        data: { ORDER_LINE: { create: lines.map(line => ({ quantity: line.quantity, COLOR_id: line.id }))}}
    });
}

async function deleteOrder(id) {
    const order = await prisma.oRDER.findUnique({ where: { id } });
    if (!order) throw { code: 404, message: "Order not found" };
    if (order.status === "IN_PROCESS") {
        throw { code: 400, message: "Order cannot be deleted" };
    }
    await prisma.oRDER.delete({ where: { id } });
}

async function cancelOrder(id) {
    const order = await prisma.oRDER.findUnique({
        where: { id },
        include: { ORDER_LINE: { include: { ITEM: true } } }
    });

    if (!order) throw { code: 404, message: "Order not found" };
    if (order.status === "COMPLETED" || order.status === "CANCELLED") {
        throw { code: 400, message: "Order status cannot be changed" };
    }

    const allItems = order.ORDER_LINE.flatMap(line => line.ITEM);

    await prisma.$transaction([
        prisma.oRDER.update({ where: { id }, data: { status: "CANCELLED" } }),
        prisma.oRDER_LINE.updateMany({ where: { ORDER_id: id }, data: { status: "CANCELLED" } }),
        prisma.iTEM.updateMany({
            where: { id: { in: allItems.map(i => i.id) } },
            data: { status: "CANCELLED" }
        })),
        prisma.iTEM_HISTORY.createMany({
            data: allItems.map(item => ({ ITEM_id: item.id, status: "CANCELLED" }))
        })
    ]);
}

module.exports = { getOrders, getOrderDetails, createOrder, deleteOrder, cancelOrder };
