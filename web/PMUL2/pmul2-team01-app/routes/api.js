var express = require('express');
var router = express.Router();

const { PrismaClient, ORDER_STATUS, ITEM_STATUS, DECISION, TEAM } = require("../generated/prisma");
const { PrismaMariaDb } = require("@prisma/adapter-mariadb");

const adapter = new PrismaMariaDb({
    host: "192.168.1.167",
    user: "team01",
    password: "team01-thebestone",
    database: "team01-database",
    port: 3307,
    connectionLimit: 5,
});

const prisma = new PrismaClient({ adapter });

module.exports = function (io) {

    // Helper to notify all connected clients
    const notifyClients = () => io.emit('db_event');

    router.get('/health', (req, res) => {
        return res.status(200).json({
            status: "UP",
            timestamp: new Date().toISOString()
        });
    });

    router.get('/orders', async function (req, res) {
        try {
            const orders = await prisma.oRDER.findMany({
                orderBy: { createdAt: 'desc' }
            });
            res.json(orders);
        } catch (error) {
            res.status(500).json({ error: error.message });
        }
    });

    router.delete('/orders/:id/delete', async function (req, res) {
        try {
            await prisma.oRDER.delete({
                where: { id: parseInt(req.params.id) }
            });
            notifyClients();
            res.sendStatus(204);
        } catch (error) {
            res.status(500).json({ error: "Deletion error" });
        }
    });

    router.post('/neworder', async function (req, res) {
        try {
            const { lines } = req.body;
            await prisma.oRDER.create({
                data: {
                    ORDER_LINE: {
                        create: lines.map(line => ({
                            quantity: line.quantity,
                            COLOR_id: line.id
                        }))
                    }
                }
            });
            notifyClients(); // Trigger Update
            res.sendStatus(204);
        } catch (error) {
            res.status(500).json({ error: "Creation error" });
        }
    });

    router.patch('/orders/:id/cancel', async function (req, res) {
        try {
            const order = await prisma.oRDER.findUnique({
                where: { id: parseInt(req.params.id) },
                include: { ORDER_LINE: { include: { ITEM: true } } }
            });

            if (!order) return res.status(404).json({ error: "Order not found" });
            if (order.status === "COMPLETED" || order.status === "CANCELLED") {
                return res.status(400).json({ error: "Order status cannot be changed" });
            }

            const allItems = order.ORDER_LINE.flatMap(line => line.ITEM);

            await prisma.$transaction([
                prisma.oRDER.update({ where: { id: order.id }, data: { status: ORDER_STATUS.CANCELLED } }),
                prisma.oRDER_LINE.updateMany({ where: { ORDER_id: order.id }, data: { status: ORDER_STATUS.CANCELLED } }),
                prisma.iTEM_HISTORY.createMany({
                    data: allItems.map(item => ({ ITEM_id: item.id, status: ITEM_STATUS.CANCELLED }))
                })
            ]);

            notifyClients(); // Trigger Update
            res.sendStatus(204);
        } catch (error) {
            res.status(500).json(error.message);
        }
    });

    router.get('/orders/:id/details', async function (req, res) {
        try {
            const id = parseInt(req.params.id);
            const order = await prisma.oRDER.findUnique({
                where: { id: id },
                include: {
                    ORDER_LINE: {
                        include: {
                            COLOR: { select: { name: true, hex: true } },
                            ITEM: { select: { id: true } }
                        }
                    }
                }
            });
            res.json(order);
        } catch (error) {
            res.status(500).json({ error: error.message });
        }
    });

    router.get('/items', async function (req, res) {
        try {
            const items = await prisma.iTEM.findMany({
                include: {
                    COLOR: { select: { name: true, hex: true } },
                    READ_CYCLE: { select: { scannedAt: true } },
                    ITEM_HISTORY: { orderBy: { createdAt: 'desc' }, take: 1, select: { status: true } },
                    SELECTION_HISTORY: { orderBy: { createdAt: 'desc' }, take: 1, select: { status: true } }
                }
            });
            res.json(items);
        } catch (error) {
            res.status(500).json({ error: error.message });
        }
    });

    router.delete('/items/:id/delete', async function (req, res) {
        try {
            await prisma.iTEM.delete({ where: { id: parseInt(req.params.id) } });
            notifyClients(); // Trigger Update
            res.sendStatus(204);
        } catch (error) {
            res.status(500).json({ error: "Deletion error" });
        }
    });

    router.get('/colors', async function (req, res) {
        try {
            const colors = await prisma.cOLOR.findMany({
                select: { name: true, hex: true, id: true }
            });
            res.json(colors);
        } catch (error) {
            res.status(500).json({ error: error.message });
        }
    });

    router.get('/scans', async function (req, res) {
        const scans = await prisma.rEAD_CYCLE.findMany({
            include: { ITEM: { include: { COLOR: true } } },
            orderBy: { scannedAt: 'desc' }
        });
        res.json(scans);
    });

    router.post('/scans', async function (req, res) {
        try {
            const { scan } = req.body;
            const readCycle = await prisma.rEAD_CYCLE.create({
                data: { qrValue: scan.qrValue, hue: scan.hue, saturation: scan.saturation, value: scan.value }
            });

            if (Object.values(TEAM).includes(scan.qrValue)) {
                const validColor = await prisma.cOLOR.findFirst({
                    where: {
                        hueMin: { lte: scan.hue }, hueMax: { gte: scan.hue },
                        saturationMin: { lte: scan.saturation }, saturationMax: { gte: scan.saturation },
                        valueMin: { lte: scan.value }, valueMax: { gte: scan.value },
                    }
                });

                if (validColor) {
                    if (scan.qrValue === TEAM.TEAM01) {
                        const orderLineInNeed = await prisma.oRDER_LINE.findFirst({
                            where: {
                                COLOR_id: validColor.id,
                                ORDER: { status: ORDER_STATUS.IN_PROCESS },
                                status: ORDER_STATUS.IN_PROCESS
                            },
                            include: { ITEM: true }
                        });

                        await prisma.iTEM.create({
                            data: {
                                team: scan.qrValue,
                                decision: orderLineInNeed ? "ORDER" : "STOCK",
                                COLOR_id: validColor.id,
                                READ_CYCLE_id: readCycle.id,
                                ORDER_LINE_id: orderLineInNeed ? orderLineInNeed.id : null,
                                ITEM_HISTORY: { create: {} },
                                SELECTION_HISTORY: { create: {} }
                            }
                        });

                        if (orderLineInNeed) {
                            await prisma.oRDER_LINE.update({
                                where: { id: orderLineInNeed.id },
                                data: {
                                    status: orderLineInNeed.ITEM.length + 1 >= orderLineInNeed.quantity ? ORDER_STATUS.COMPLETED : ORDER_STATUS.IN_PROCESS
                                }
                            });

                            const pendingLines = await prisma.oRDER_LINE.count({
                                where: { ORDER_id: orderLineInNeed.ORDER_id, status: ORDER_STATUS.IN_PROCESS }
                            });

                            if (pendingLines === 0) {
                                await prisma.oRDER.update({
                                    where: { id: orderLineInNeed.ORDER_id },
                                    data: { status: ORDER_STATUS.COMPLETED, completedAt: new Date() }
                                });
                            }
                        }
                    } else {
                        await prisma.iTEM.create({
                            data: {
                                team: scan.qrValue,
                                decision: DECISION.PASS,
                                COLOR_id: validColor.id,
                                READ_CYCLE_id: readCycle.id,
                                ITEM_HISTORY: { create: {} },
                                SELECTION_HISTORY: { create: {} }
                            }
                        });
                    }
                }
            }
            notifyClients(); // Trigger refresh for everything (scans, items, orders)
            res.sendStatus(204);
        } catch (error) {
            res.status(500).json({ error: error.message });
        }
    });

    return router;
};