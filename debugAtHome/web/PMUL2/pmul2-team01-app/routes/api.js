var express = require('express');
var router = express.Router();

const { getScans, createScan, deleteScan } = require("../services/scan");
const { getOrders, getOrderDetails, createOrder, deleteOrder, cancelOrder, getCurrentOrder } = require("../services/order");
const { getItems, deleteItem, updateItemStatus, getLogs, deleteLog, clearLogs, getItemDetails } = require("../services/item");
const { getColors, updateColors, initColors, saveAsJson } = require("../services/color");
const { getStats, resetColorStats, incrementColorStat } = require("../services/stats");

module.exports = function (io) {
    const pyLogs = [];
    let sensors = [
        { name: "IR SCAN",  state: 0 },
        { name: "IR NEXT",  state: 0 },
        { name: "IR STOCK", state: 0 },
        { name: "IR ORDER", state: 0 },
        { name: "IR PASS",  state: 0 },
    ];
    const notifyClients = () => io.emit('db_event');

    // handle pour eviter de repeter le try/catch partout
    const handle = (fn) => async (req, res) => {
        try {
            await fn(req, res);
        } catch (error) {
            const code = error.status || error.code || 500;

            console.error("API error : " + error.message)
            res.status(code).json({ error: error.message }); // ← actually respond
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

    router.get('/orders/current', handle(async (req, res) => {
        res.json(await getCurrentOrder());
    }));

    router.post('/neworder', handle(async (req, res) => {
        res.json(await createOrder(req.body.lines));
        io.emit('order_event');
        notifyClients();
    }));

    router.patch('/orders/:id/cancel', handle(async (req, res) => {
        await cancelOrder(parseInt(req.params.id));
        io.emit('order_event');
        notifyClients();
        res.sendStatus(204);
    }));

    router.delete('/orders/:id/delete', handle(async (req, res) => {
        await deleteOrder(parseInt(req.params.id));
        notifyClients();
        io.emit('order_event');
        res.sendStatus(204);
    }));

    //Items

    router.get('/items', handle(async (req, res) => {
        res.json(await getItems());
    }));

    router.get('/items/:id', handle(async (req, res) => {
        res.json(await getItemDetails(parseInt(req.params.id)));
    }));

    router.get('/logs', handle(async (req, res) => {
        res.json(await getLogs());
    }));

    router.delete('/logs/:id/delete', handle(async (req, res) => {
        await deleteLog(parseInt(req.params.id));
        notifyClients();
        res.sendStatus(204);
    }));

    router.delete('/logs/clear', handle(async (req, res) => {
        await clearLog();
        notifyClients();
        res.sendStatus(204);
    }));

    router.delete('/items/:id/delete', handle(async (req, res) => {
        await deleteItem(parseInt(req.params.id));
        notifyClients();
        res.sendStatus(204);
    }));

    router.patch('/items/:id/status', handle(async (req, res) => {
        result = await updateItemStatus(parseInt(req.params.id), req.body.status)
        res.json(result)
        notifyClients();
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

    router.post('/colors/json', handle(async (req, res) => {
        await saveAsJson();
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

    router.post('/stats/reset', handle (async(req, res) => {
        resetColorStats();
        notifyClients();
        res.sendStatus(204);
    }));

    //Python Logs
    router.post('/python/logs', (req, res) => {
        const { msg, time } = req.body;
        pyLogs.unshift({ msg, time }); // plus récent en premier
        if (pyLogs.length > 200) pyLogs.pop(); // limite
        io.emit('py_log', { msg, time });
        res.sendStatus(204);
    });

    router.get('/python/logs', (req, res) => {
        res.json(pyLogs);
    });

    //Sensors
    router.post('/sensors', handle ((req, res) => {
        sensors = req.body.sensors;
        io.emit('sensor_event');
        res.sendStatus(204);
    }));

    router.get('/sensors', handle ((req, res) => {
       res.json(sensors);
    }));

    return router;
};
