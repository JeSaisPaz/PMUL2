var express = require('express');
var router = express.Router();

router.get('/', function(req, res, next) {
  res.render('dashboard', { title: 'Dashboard' });
});

router.get('/orders', function(req, res, next) {
  res.render('orders', { title: 'Orders' });
});

router.get('/orders/:id', function(req, res) {
  res.render('orderDetails', { 
      title: "Order #" + req.params.id,
      id: req.params.id 
  });
});

router.get('/neworder', function(req, res, next) {
  res.render('neworder', { title: 'Place an order' });
});

router.get('/items/:id', function(req, res) {
  res.render('itemTracking', { 
    title: "Item #" + req.params.id,
    id: req.params.id 
  });
});

router.get('/items', function (req, res) {
  res.render('items', { title: 'Items' });
});

router.get('/scans', function(req, res, next) {
  res.render('scans', { title: 'Scans' });
});

router.get('/colors', function(req, res, next) {
  res.render('colors', { title: 'Colors' });
});

router.get('/logs', function(req, res, next) {
  res.render('logs', { title: 'Logs' });
});

module.exports = router;
