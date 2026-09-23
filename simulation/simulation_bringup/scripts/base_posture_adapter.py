#!/usr/bin/env python3
"""MuJoCo implementation of the B2 base-posture service contract.

The HMI bridge always calls the platform services `/stand_up`, `/stand_down`,
`/balance_stand`, `/recovery_stand`, and `/damp`.  Hardware implements them
through SDK2; MuJoCo owns this adapter instead of asking the bridge to pretend
that a missing service succeeded.
"""

import rclpy
from rclpy.node import Node
from std_srvs.srv import Trigger


class BasePostureAdapter(Node):
    """Expose only posture transitions that the current MuJoCo model represents."""

    def __init__(self) -> None:
        super().__init__("base_posture_adapter")
        self._services = []
        for posture in ("stand_up", "stand_down", "balance_stand", "recovery_stand", "damp"):
            self._services.append(
                self.create_service(Trigger, posture, self._callback_for(posture))
            )

    def _callback_for(self, posture: str):
        def callback(_request: Trigger.Request, response: Trigger.Response) -> Trigger.Response:
            # The policy runner starts and remains in its standing control mode.
            # Claiming that sit, recovery, or damp happened would make the HMI
            # show a state that MuJoCo did not actually enact.
            if posture in ("stand_up", "balance_stand"):
                response.success = True
                response.message = "MuJoCo B2 is in standing control mode"
            else:
                response.success = False
                response.message = f"MuJoCo does not model '{posture}'"
            return response

        return callback


def main() -> None:
    rclpy.init()
    node = BasePostureAdapter()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
