from processing import *
from dataclasses import dataclass, asdict
import struct

GYRO_DATA,ACC_DATA,LACC_DATA,GACC_DATA,QUAT_DATA,STAT_DATA = 0,1,2,3,4,5

data_type_len = {GYRO_DATA:6,ACC_DATA:6,LACC_DATA:6,GACC_DATA:6,QUAT_DATA:8,STAT_DATA:1}

@dataclass
class gyro:
    x: float
    y: float
    z: float

class quat:
    w: float
    x: float
    y: float
    z: float

@dataclass
class lacc:
    x: float
    y: float
    z: float

@dataclass
class stat:
    mag: int
    acc: int
    gyro: int  
    sys: int

# Accel data registers
BNO055_ACCEL_DATA_X_LSB_ADDR = 0X08
BNO055_ACCEL_DATA_X_MSB_ADDR = 0X09
BNO055_ACCEL_DATA_Y_LSB_ADDR = 0X0A
BNO055_ACCEL_DATA_Y_MSB_ADDR = 0X0B
BNO055_ACCEL_DATA_Z_LSB_ADDR = 0X0C
BNO055_ACCEL_DATA_Z_MSB_ADDR = 0X0D

# Mag data registers
BNO055_MAG_DATA_X_LSB_ADDR = 0X0E
BNO055_MAG_DATA_X_MSB_ADDR = 0X0F
BNO055_MAG_DATA_Y_LSB_ADDR = 0X10
BNO055_MAG_DATA_Y_MSB_ADDR = 0X11
BNO055_MAG_DATA_Z_LSB_ADDR = 0X12
BNO055_MAG_DATA_Z_MSB_ADDR = 0X13

# Gyro data registers
BNO055_GYRO_DATA_X_LSB_ADDR = 0X14
BNO055_GYRO_DATA_X_MSB_ADDR = 0X15
BNO055_GYRO_DATA_Y_LSB_ADDR = 0X16
BNO055_GYRO_DATA_Y_MSB_ADDR = 0X17
BNO055_GYRO_DATA_Z_LSB_ADDR = 0X18
BNO055_GYRO_DATA_Z_MSB_ADDR = 0X19

#E uler data registers
BNO055_EULER_H_LSB_ADDR = 0X1A
BNO055_EULER_H_MSB_ADDR = 0X1B
BNO055_EULER_R_LSB_ADDR = 0X1C
BNO055_EULER_R_MSB_ADDR = 0X1D
BNO055_EULER_P_LSB_ADDR = 0X1E
BNO055_EULER_P_MSB_ADDR = 0X1F

# Quaternion data registers
BNO055_QUATERNION_DATA_W_LSB_ADDR = 0X20
BNO055_QUATERNION_DATA_W_MSB_ADDR = 0X21
BNO055_QUATERNION_DATA_X_LSB_ADDR = 0X22
BNO055_QUATERNION_DATA_X_MSB_ADDR = 0X23
BNO055_QUATERNION_DATA_Y_LSB_ADDR = 0X24
BNO055_QUATERNION_DATA_Y_MSB_ADDR = 0X25
BNO055_QUATERNION_DATA_Z_LSB_ADDR = 0X26
BNO055_QUATERNION_DATA_Z_MSB_ADDR = 0X27

#/ Linear acceleration data registers
BNO055_LINEAR_ACCEL_DATA_X_LSB_ADDR = 0X28
BNO055_LINEAR_ACCEL_DATA_X_MSB_ADDR = 0X29
BNO055_LINEAR_ACCEL_DATA_Y_LSB_ADDR = 0X2A
BNO055_LINEAR_ACCEL_DATA_Y_MSB_ADDR = 0X2B
BNO055_LINEAR_ACCEL_DATA_Z_LSB_ADDR = 0X2C
BNO055_LINEAR_ACCEL_DATA_Z_MSB_ADDR = 0X2D

# Gravity data registers
BNO055_GRAVITY_DATA_X_LSB_ADDR = 0X2E
BNO055_GRAVITY_DATA_X_MSB_ADDR = 0X2F
BNO055_GRAVITY_DATA_Y_LSB_ADDR = 0X30
BNO055_GRAVITY_DATA_Y_MSB_ADDR = 0X31
BNO055_GRAVITY_DATA_Z_LSB_ADDR = 0X32
BNO055_GRAVITY_DATA_Z_MSB_ADDR = 0X33

# Temperature data register
BNO055_TEMP_ADDR = 0X34

# Status registers
BNO055_CALIB_STAT_ADDR = 0X35

BURST_READ_BASE_ADDR = BNO055_GYRO_DATA_X_LSB_ADDR

GACC_SCALE_FACTOR = 100.0 #m/s^2
QUAT_SCALE_FACTOR = float((1 << 14))
LACC_SCALE_FACTOR = 100.0 #m/s^2
GYRO_SCALE_FACTOR = 16.0
ACC_SCALE_FACTOR = 100.0 #m/s^2

def unpack_imu_data_single_type(data_single, data_type):
    if data_type == GYRO_DATA:
        gx,gy,gz = struct.unpack('<3h',data_single)
        return gyro(gx/GYRO_SCALE_FACTOR,gy/GYRO_SCALE_FACTOR,gz/GYRO_SCALE_FACTOR)
    if data_type == QUAT_DATA:
        qw,qx,qy,qz = struct.unpack('<4h',data_single)
        return quat(qw/QUAT_SCALE_FACTOR,qx/QUAT_SCALE_FACTOR,qy/QUAT_SCALE_FACTOR,qz/QUAT_SCALE_FACTOR)
    if data_type == LACC_DATA:
        laccx,laccy,laccz = struct.unpack('<3h',data_single)
        return lacc(laccx/LACC_SCALE_FACTOR ,laccy/LACC_SCALE_FACTOR ,laccz/LACC_SCALE_FACTOR)
    if data_type == ACC_DATA:
        accx,accy,accz = struct.unpack('<3h',data_single)
        return acc(accx/LACC_SCALE_FACTOR ,accy/LACC_SCALE_FACTOR ,accz/LACC_SCALE_FACTOR)
    if data_type == STAT_DATA:
        reg_value = data_single[0]
        calib_mag = reg_value & 0x03
        calib_accel = (reg_value >> 2) & 0x03
        calib_gyro = (reg_value >> 4) & 0x03
        calib_sys = (reg_value >> 6) & 0x03
        return stat(calib_mag,calib_accel,calib_gyro,calib_sys)

def dataclass_to_list(d):
    return list(asdict(d).values())

def unpack_imu_data(data, data_type_seq):
    """
    unpack multi-byte multi-type imu data according to seq defined in data_type_seq
    returns an array with all elements in the order of data_type_seq
    the available types are defined in data_type_len dict
    """
    all_elements = []
    current_index = 0
    for t in data_type_seq:
        unpacked = unpack_imu_data_single_type(data[current_index:current_index+data_type_len[t]],t)
        current_index+=data_type_len[t]
        all_elements = all_elements + dataclass_to_list(unpacked)
    return all_elements

#a = load_data("~/Downloads/Unknown.pkl")
a = load_data("./Unknown.pkl")
data_types = [STAT_DATA,LACC_DATA,GYRO_DATA]
for segment in a.loc["i"]:
    for ts, data in segment:
        print(ts, unpack_imu_data(data,data_types))