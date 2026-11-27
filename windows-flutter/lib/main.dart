import 'dart:io';

import 'package:flutter/widgets.dart';

import 'core/smoke.dart';

Future<void> main(List<String> args) async {
  WidgetsFlutterBinding.ensureInitialized();
  if (args.isNotEmpty && args[0] == '--smoke-test') {
    exit(await runSmokeTest(args[1], args[2]));
  }
}
