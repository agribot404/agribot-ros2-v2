import os

filepath = "esp-drive-node/src/main.cpp"
with open(filepath, "r") as f:
    content = f.read()

# Add mutex declaration
if "SemaphoreHandle_t ros_mutex_;" not in content:
    content = content.replace("private:", "private:\n    SemaphoreHandle_t ros_mutex_;")

# Initialize mutex in begin()
if "ros_mutex_ = xSemaphoreCreateMutex();" not in content:
    content = content.replace("Serial.println(\"[drive] Booting...\");", "Serial.println(\"[drive] Booting...\");\n        ros_mutex_ = xSemaphoreCreateMutex();")

# Wrap spin
spin_old = "rclc_executor_spin_some(&self->executor_, RCL_MS_TO_NS(10));"
spin_new = """if (xSemaphoreTake(self->ros_mutex_, pdMS_TO_TICKS(50))) {
                rclc_executor_spin_some(&self->executor_, RCL_MS_TO_NS(10));
                xSemaphoreGive(self->ros_mutex_);
            }"""
content = content.replace(spin_old, spin_new)

# Wrap publish
pub_old = """                rcl_publish(&self->pub_sonar_front_,
                            &self->msg_sonar_front_, NULL);
                rcl_publish(&self->pub_sonar_rear_,
                            &self->msg_sonar_rear_, NULL);"""
pub_new = """                if (xSemaphoreTake(self->ros_mutex_, pdMS_TO_TICKS(50))) {
                    rcl_publish(&self->pub_sonar_front_, &self->msg_sonar_front_, NULL);
                    rcl_publish(&self->pub_sonar_rear_, &self->msg_sonar_rear_, NULL);
                    xSemaphoreGive(self->ros_mutex_);
                }"""
content = content.replace(pub_old, pub_new)

with open(filepath, "w") as f:
    f.write(content)

