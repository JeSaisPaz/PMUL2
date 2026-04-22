import serial
import time
import os
import cv2
import mysql.connector
import numpy as np
from pyzbar.pyzbar import decode
from PIL import Image
from picamera2 import Picamera2, Preview

mydb = mysql.connector.connect(
  host="ip",
  user="username",
  password="password"
)


