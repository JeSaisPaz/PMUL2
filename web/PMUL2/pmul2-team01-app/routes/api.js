var express = require('express');
var router = express.Router();

const { PrismaClient, ORDER_STATUS, ITEM_STATUS, DECISION, TEAM, DECISION_STATUS } = require("../generated/prisma");

require("dotenv").config();
const { PrismaMariaDb } = require("@prisma/adapter-mariadb");
const adapter = new PrismaMariaDb({
    host: "mysql",
    user: process.env.MYSQL_USER,
    password: process.env.MYSQL_PASSWORD,
    database: process.env.MYSQL_DATABASE,
    port: 3306,
    connectionLimit: 5,
});

const prisma = new PrismaClient({ adapter });

module.exports = function (io, piBridge) {

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
            const order = await prisma.oRDER.findUnique({
                where: { id: parseInt(req.params.id) },
            });

            if (!order) return res.status(404).json({ error: "Order not found" });
            if (order.status === ORDER_STATUS.IN_PROCESS) {
                return res.status(400).json({ error: "Order cannot be deleted" });
            }
            await prisma.oRDER.delete({ where: { id: order.id } });
            notifyClients();
            res.sendStatus(204);
        } catch (error) {
            res.status(500).json({ error: error.message });
        }
    });

    router.post('/neworder', async function (req, res) {
        try {
            const { lines } = req.body;
            if (!Array.isArray(lines) || lines.length === 0) {
                return res.status(400).json({ error: "Order must contain at least one line." });
            }

            var newOrder = await prisma.oRDER.create({
                data: {
                    ORDER_LINE: {
                        create: lines.map(line => ({
                            quantity: line.quantity,
                            COLOR_id: line.id
                        }))
                    }
                }
            });
            notifyClients();
            piBridge.trySendOrder(newOrder.id); // on balance la commande au Pi (ou dans sa file d'attente)
            res.sendStatus(204);
        } catch (error) {
            res.status(500).json({ error: error.message });
        }
    });

    router.patch('/orders/:id/cancel', async function (req, res) {
        try {
            const order = await prisma.oRDER.findUnique({
                where: { id: parseInt(req.params.id) },
                include: { ORDER_LINE: { include: { ITEM: true } } }
            });

            if (!order) return res.status(404).json({ error: "Order not found" });
            if (order.status === ORDER_STATUS.COMPLETED || order.status === ORDER_STATUS.CANCELLED) {
                return res.status(400).json({ error: "Order status cannot be changed" });
            }

            const allItems = order.ORDER_LINE.flatMap(line => line.ITEM);

            await prisma.$transaction([
                prisma.oRDER.update({ where: { id: order.id }, data: { status: ORDER_STATUS.CANCELLED } }),
                prisma.oRDER_LINE.updateMany({ where: { ORDER_id: order.id }, data: { status: ORDER_STATUS.CANCELLED } }),
                prisma.iTEM.updateMany({
                    where: { id: { in: allItems.map(i => i.id) } },
                    data: { status: ITEM_STATUS.CANCELLED }
                }),
                prisma.iTEM_HISTORY.createMany({
                    data: allItems.map(item => ({ ITEM_id: item.id, status: ITEM_STATUS.CANCELLED }))
                })
            ]);

            notifyClients();
            piBridge.trySendOrder(); // si la commande annulee bloquait la file
            res.sendStatus(204);
        } catch (error) {
            res.status(500).json(error.message);
        }
    });

    router.get('/orders/:id/details', async function (req, res) {
        try {
            const id = parseInt(req.params.id);
            const order = await prisma.oRDER.findUnique({
                where: { id },
                include: {
                    ORDER_LINE: {
                        include: {
                            COLOR: { select: { name: true, hex: true } },
                            ITEM: { select: { id: true, status: true } }
                        }
                    }
                }
            });

            if (!order) return res.status(404).json({ error: "Order not found" });

            const result = {
                ...order,
                ORDER_LINE: order.ORDER_LINE.map(line => ({
                    ...line,
                    orderedCount:   line.ITEM.filter(i => i.status === ITEM_STATUS.ORDERED).length,
                    inProcessCount: line.ITEM.filter(i => i.status === ITEM_STATUS.IN_PROCESS).length,
                    cancelledCount: line.ITEM.filter(i => i.status === ITEM_STATUS.CANCELLED).length,
                }))
            };

            res.json(result);
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
                }
            });
            res.json(items);
        } catch (error) {
            res.status(500).json({ error: error.message });
        }
    });

    router.delete('/items/:id/delete', async function (req, res) {
        try {
            const item = await prisma.iTEM.findUnique({
                where: { id: parseInt(req.params.id) },
                include: { ORDER_LINE: { select: { status: true } } }
            });
            if (!item) return res.status(404).json({ error: "Item not found" });

            if (item.status === ITEM_STATUS.IN_PROCESS || item.ORDER_LINE?.status === ORDER_STATUS.IN_PROCESS || item.ORDER_LINE?.status === ORDER_STATUS.COMPLETED) {
                return res.status(400).json({ error: "Item cannot be deleted" });
            }

            await prisma.iTEM.delete({ where: { id: item.id } });
            notifyClients();
            res.sendStatus(204);
        } catch (error) {
            res.status(500).json({ error: error.message });
        }
    });

    router.patch('/items/:id/status', async function (req, res) {
        try {
            const { status } = req.body;

            const item = await prisma.iTEM.findUnique({
                where: { id: parseInt(req.params.id) },
                include: {
                    ORDER_LINE: { include: { ORDER: true } }
                }
            });

            if (!item) return res.status(404).json({ error: "Item not found" });
            if (!Object.values(DECISION_STATUS).includes(status.status)) {
                return res.status(400).json({ error: "Invalid status" });
            }
            if (item.decisionStatus !== DECISION_STATUS.IN_PROCESS || item.status !== ITEM_STATUS.IN_PROCESS) {
                return res.status(400).json({ error: "Item is not in a processable state" });
            }

            if (item.decision === DECISION.PASS) {
                const newStatus = status.status === DECISION_STATUS.CONFIRMED ? ITEM_STATUS.EXTERNAL : ITEM_STATUS.CANCELLED;
                await prisma.$transaction([
                    prisma.iTEM.update({ where: { id: item.id }, data: { status: newStatus, decisionStatus: status.status } }),
                    prisma.iTEM_HISTORY.create({ data: { ITEM_id: item.id, status: newStatus } }),
                    prisma.sELECTION_HISTORY.create({ data: { ITEM_id: item.id, status: status.status } })
                ]);
            }

            else if (item.decision === DECISION.STOCK) {
                const newStatus = status.status === DECISION_STATUS.CONFIRMED ? ITEM_STATUS.AVAILABLE : ITEM_STATUS.CANCELLED;
                await prisma.$transaction([
                    prisma.iTEM.update({ where: { id: item.id }, data: { status: newStatus, decisionStatus: status.status } }),
                    prisma.iTEM_HISTORY.create({ data: { ITEM_id: item.id, status: newStatus } }),
                    prisma.sELECTION_HISTORY.create({ data: { ITEM_id: item.id, status: status.status } })
                ]);
            }

            else if (item.decision === DECISION.ORDER) {
                const newStatus = status.status === DECISION_STATUS.CONFIRMED ? ITEM_STATUS.ORDERED : ITEM_STATUS.CANCELLED;

                if (item.ORDER_LINE?.ORDER.status !== ORDER_STATUS.CANCELLED) {
                    await prisma.$transaction([
                        prisma.iTEM.update({ where: { id: item.id }, data: { status: newStatus, decisionStatus: status.status } }),
                        prisma.iTEM_HISTORY.create({ data: { ITEM_id: item.id, status: newStatus } }),
                        prisma.sELECTION_HISTORY.create({ data: { ITEM_id: item.id, status: status.status } })
                    ]);
                    const currentLine = await prisma.oRDER_LINE.findUnique({
                        where: { id: item.ORDER_LINE_id },
                        include: {
                            ITEM: { where: { status: ITEM_STATUS.ORDERED } },
                            ORDER: true
                        }
                    });

                    const countOrdered = currentLine.ITEM.length;

                    if (countOrdered >= currentLine.quantity) {
                        await prisma.oRDER_LINE.update({
                            where: { id: currentLine.id },
                            data: { status: ORDER_STATUS.COMPLETED }
                        });

                        const pendingLines = await prisma.oRDER_LINE.count({
                            where: { ORDER_id: currentLine.ORDER_id, status: ORDER_STATUS.IN_PROCESS }
                        });

                        if (pendingLines === 0) {
                            await prisma.oRDER.update({
                                where: { id: currentLine.ORDER_id },
                                data: { status: ORDER_STATUS.COMPLETED, completedAt: new Date() }
                            });
                        }
                    }
                } else {
                    await prisma.$transaction([
                        prisma.iTEM.update({ where: { id: item.id }, data: { status: newStatus, decisionStatus: status.status } }),
                        prisma.sELECTION_HISTORY.create({ data: { ITEM_id: item.id, status: status.status } })
                    ]);
                }
            }

            notifyClients();
            res.sendStatus(204);
        } catch (error) {
            res.status(500).json({ error: error.message });
        }
    });

    router.get('/colors', async function (req, res) {
        try {
            const colors = await prisma.cOLOR.findMany();
            res.json(colors);
        } catch (error) {
            res.status(500).json({ error: error.message });
        }
    });

    router.get('/scans', async function (req, res) {
        try {
            const scans = await prisma.rEAD_CYCLE.findMany({
                include: { ITEM: { include: { COLOR: true } } },
                orderBy: { scannedAt: 'desc' }
            });
            res.json(scans);
        } catch (error) {
            res.status(500).json({ error: error.message });
        }
    });

    router.delete('/scans/:id/delete', async function (req, res) {
        try {
            await prisma.rEAD_CYCLE.delete({ where: { id: parseInt(req.params.id) } });
            notifyClients();
            res.sendStatus(204);
        } catch (error) {
            res.status(500).json({ error: error.message });
        }
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
                        const orderLines = await prisma.oRDER_LINE.findMany({
                            where: {
                                COLOR_id: validColor.id,
                                ORDER: { status: ORDER_STATUS.IN_PROCESS },
                                status: ORDER_STATUS.IN_PROCESS,
                            },
                            orderBy: { ORDER: { createdAt: 'asc' } },
                            include: {
                                ITEM: {
                                    where: {
                                        status: { in: [ITEM_STATUS.ORDERED, ITEM_STATUS.IN_PROCESS] }
                                    }
                                }
                            }
                        });

                        const orderLineInNeed = orderLines.find(line => line.ITEM.length < line.quantity) ?? null;
                        const hasRoom = orderLineInNeed !== null;

                        await prisma.iTEM.create({
                            data: {
                                team: scan.qrValue,
                                decision: hasRoom ? DECISION.ORDER : DECISION.STOCK,
                                COLOR_id: validColor.id,
                                READ_CYCLE_id: readCycle.id,
                                ORDER_LINE_id: hasRoom ? orderLineInNeed.id : null,
                                ITEM_HISTORY: { create: {} },
                                SELECTION_HISTORY: { create: {} }
                            }
                        });
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

            notifyClients();
            res.sendStatus(204);
        } catch (error) {
            res.status(500).json({ error: error.message });
        }
    });

    return router;
};