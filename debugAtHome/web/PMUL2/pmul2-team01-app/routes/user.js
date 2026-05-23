var express = require('express');
var router = express.Router();

const handle = (fn) => async (req, res, next) => {
    try {
        await fn(req, res, next);
    } catch (error) {
        console.error("Routing error : " + error.message);
        // On renvoie une erreur 500 explicite pour voir le problème côté client
        res.status(500).send("Server error : " + error.message);
    }
};

router.get('/', handle(async (req, res) => {
    res.render('dashboard', { title: 'Dashboard' });
}));

router.get('/orders', handle(async (req, res) => {
    res.render('orders', { title: 'Orders' });
}));

router.get('/orders/:id', handle(async (req, res) => {
    res.render('orderDetails', { 
        title: "Order #" + req.params.id,
        id: req.params.id 
    });
}));

router.get('/neworder', handle(async (req, res) => {
    res.render('neworder', { title: 'Place an order' });
}));

router.get('/items/:id', handle(async (req, res) => {
    res.render('itemTracking', { 
        title: "Item #" + req.params.id,
        id: req.params.id 
    });
}));

router.get('/items', handle(async (req, res) => {
    res.render('items', { title: 'Items' });
}));

router.get('/scans', handle(async (req, res) => {
    res.render('scans', { title: 'Scans' });
}));

router.get('/colors', handle(async (req, res) => {
    res.render('colors', { title: 'Colors' });
}));

router.get('/logs', handle(async (req, res) => {
    res.render('logs', { title: 'Logs' });
}));

module.exports = router;