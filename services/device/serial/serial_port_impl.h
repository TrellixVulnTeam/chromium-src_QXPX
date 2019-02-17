// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_DEVICE_SERIAL_SERIAL_PORT_IMPL_H_
#define SERVICES_DEVICE_SERIAL_SERIAL_PORT_IMPL_H_

#include <string>
#include <vector>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "mojo/public/cpp/system/simple_watcher.h"
#include "services/device/public/mojom/serial.mojom.h"

namespace base {
class SingleThreadTaskRunner;
}

namespace device {

class SerialIoHandler;

// TODO(leonhsl): Merge this class with SerialIoHandler if/once
// SerialIoHandler is exposed only via the Device Service.
// crbug.com/748505
// This class must be constructed and run on IO thread.
class SerialPortImpl : public mojom::SerialPort {
 public:
  static void Create(
      const base::FilePath& path,
      mojom::SerialPortRequest request,
      scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner);

  SerialPortImpl(const base::FilePath& path,
                 scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner);
  ~SerialPortImpl() override;

 private:
  // mojom::SerialPort methods:
  void Open(mojom::SerialConnectionOptionsPtr options,
            mojo::ScopedDataPipeProducerHandle out_stream,
            mojom::SerialPortClientAssociatedPtrInfo client,
            OpenCallback callback) override;
  void Write(const std::vector<uint8_t>& data, WriteCallback callback) override;
  void ClearReadError(mojo::ScopedDataPipeProducerHandle producer) override;
  void CancelWrite(mojom::SerialSendError reason) override;
  void Flush(FlushCallback callback) override;
  void GetControlSignals(GetControlSignalsCallback callback) override;
  void SetControlSignals(mojom::SerialHostControlSignalsPtr signals,
                         SetControlSignalsCallback callback) override;
  void ConfigurePort(mojom::SerialConnectionOptionsPtr options,
                     ConfigurePortCallback callback) override;
  void GetPortInfo(GetPortInfoCallback callback) override;
  void SetBreak(SetBreakCallback callback) override;
  void ClearBreak(ClearBreakCallback callback) override;

  void OnOpenCompleted(OpenCallback callback, bool success);
  void ReadFromPortAndWriteOut(MojoResult result,
                               const mojo::HandleSignalsState& state);
  void WriteToOutStream(int bytes_read, mojom::SerialReceiveError error);

  scoped_refptr<SerialIoHandler> io_handler_;
  mojom::SerialPortClientAssociatedPtr client_;
  mojo::ScopedDataPipeProducerHandle out_stream_;
  mojo::SimpleWatcher out_stream_watcher_;

  base::WeakPtrFactory<SerialPortImpl> weak_factory_;
  DISALLOW_COPY_AND_ASSIGN(SerialPortImpl);
};

}  // namespace device

#endif  // SERVICES_DEVICE_SERIAL_SERIAL_PORT_IMPL_H_