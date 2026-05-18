var express = require('express');
var router = express.Router();

const { getScans, createScan, deleteScan } = require("../services/scan");
const { getOrders, getOrderDetails, createOrder, deleteOrder, cancelOrder } = require("../services/order");
const { getItems, deleteItem, updateItemStatus } = require("../services/item");
const { getColors, updateColors, initColors } = require("../services/color");
const { getStats, resetColorStats, incrementColorStat } = require("../services/stats");

module.exports = function (io) {

    const notifyClients = () => io.emit('db_event');

    // handle pour eviter de repeter le try/catch partout
    const handle = (fn) => async (req, res) => {
        try {
            await fn(req, res);
        } catch (error) {
            const code = error.code || 500;
            res.status(code).json({ error: error.message});
            console.log(error.message)
        }
    };

    router.get('/health', (req, res) => {
        return res.status(200).json({ status: "UP", timestamp: new Date().toISOString() });
    });

    //Orders

    router.get('/orders', handle(async (req, res) => {
        res.json(await getOrders());
    }));

    router.get('/orders/:id/details', handle(async (req, res) => {
        res.json(await getOrderDetails(parseInt(req.params.id)));
    }));

    router.post('/neworder', handle(async (req, res) => {
        res.json(await createOrder(req.body.lines));
        notifyClients();
    }));

    router.patch('/orders/:id/cancel', handle(async (req, res) => {
        await cancelOrder(parseInt(req.params.id));
        notifyClients();
        res.sendStatus(204);
    }));

    router.delete('/orders/:id/delete', handle(async (req, res) => {
        await deleteOrder(parseInt(req.params.id));
        notifyClients();
        res.sendStatus(204);
    }));

    //Items

    router.get('/items', handle(async (req, res) => {
        res.json(await getItems());
    }));

    router.delete('/items/:id/delete', handle(async (req, res) => {
        await deleteItem(parseInt(req.params.id));
        notifyClients();
        res.sendStatus(204);
    }));

    router.patch('/items/:id/status', handle(async (req, res) => {
        await updateItemStatus(parseInt(req.params.id), req.body.status);
        notifyClients();
        res.sendStatus(204);
    }));

    //Colors

    router.get('/colors', handle(async (req, res) => {
        res.json(await getColors());
    }));

    router.put('/colors/:name/update', handle(async (req, res) => {
        await updateColors(req.body.color);
        notifyClients();
        res.sendStatus(204);
    }));

    router.post('/colors/init', handle(async (req, res) => {
        await initColors();
        notifyClients();
        res.sendStatus(204);
    }));

    //Scans

    router.get('/scans', handle(async (req, res) => { 
        res.json(await getScans());
    }));

    router.post('/scans', handle(async (req, res) => {
        res.status(201).json(await createScan(req.body.scan)); //code 201 HTTP CREATED on envoie itemId, decision et orderId si Order
        notifyClients();
    }));

    router.delete('/scans/:id/delete', handle(async (req, res) => {
        await deleteScan(parseInt(req.params.id));
        notifyClients();
        res.sendStatus(204);
    }));

    //Stats

    router.get('/stats', handle(async (req, res) => { 
        res.json(await getStats());
    }));

    router.post('/stats/reset', (req, res) => {
        resetColorStats();
        notifyClients();
        res.sendStatus(204);
    });

    return router;
};
