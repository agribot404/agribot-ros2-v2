import os

filepath = "esp-payload-node/src/main.cpp"
with open(filepath, "r") as f:
    content = f.read()

# Add mutex declaration
if "SemaphoreHandle_t ros_mutex_;" not in content:
    content = content.replace("private:", "private:\n    SemaphoreHandle_t ros_mutex_;")

# Initialize mutex in begin()
if "ros_mutex_ = xSemaphoreCreateMutex();" not in content:
    content = content.replace("Serial.println(\"[payload] Booting...\");", "Serial.println(\"[payload] Booting...\");\n        ros_mutex_ = xSemaphoreCreateMutex();")

# Wrap spin
spin_old = "rclc_executor_spin_some(&self->executor_, RCL_MS_TO_NS(10));"
spin_new = """if (xSemaphoreTake(self->ros_mutex_, pdMS_TO_TICKS(50))) {
                rclc_executor_spin_some(&self->executor_, RCL_MS_TO_NS(10));
                xSemaphoreGive(self->ros_mutex_);
            }"""
content = content.replace(spin_old, spin_new)

# Wrap publish temp
pub_old_temp = "rcl_publish(&self->pub_temperature_, &self->msg_temp_, NULL);"
pub_new_temp = """if (xSemaphoreTake(self->ros_mutex_, pdMS_TO_TICKS(50))) {
                        rcl_publish(&self->pub_temperature_, &self->msg_temp_, NULL);
                        xSemaphoreGive(self->ros_mutex_);
                    }"""
content = content.replace(pub_old_temp, pub_new_temp)

# Wrap publish hum
pub_old_hum = "rcl_publish(&self->pub_humidity_, &self->msg_hum_, NULL);"
pub_new_hum = """if (xSemaphoreTake(self->ros_mutex_, pdMS_TO_TICKS(50))) {
                        rcl_publish(&self->pub_humidity_, &self->msg_hum_, NULL);
                        xSemaphoreGive(self->ros_mutex_);
                    }"""
content = content.replace(pub_old_hum, pub_new_hum)

with open(filepath, "w") as f:
    f.write(content)

